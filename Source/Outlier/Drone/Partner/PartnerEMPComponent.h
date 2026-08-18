#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "GameplayTagContainer.h"
#include "UI/UILayerTypes.h"
#include "PartnerEMPComponent.generated.h"

class UEMPableComponent;
class UEMPLayerWidget;
class UEMPMarkWidget;
class ULocalPlayerUILayerSubsystem;

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerEMPAbilityData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP")
	float EMPRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP")
	float MarkingTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP")
	float StunDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP")
	int32 MaxTargets = 99;
};

UCLASS()
class OUTLIER_API UPartnerEMPComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerEMPComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	float EMPRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Marking")
	float EMPMarkingTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP|Early Complete", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float EMPEarlyCompleteValue = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EMP|CancleTime")
	float EMPInitialCaptureEmptyTimeout = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	FGameplayTagContainer RequiredEMPTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	FGameplayTagContainer BlockedEMPTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	uint8 bRequireLineOfSight : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Debug")
	uint8 bDebugEMP : 1 = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP|UI")
	TSubclassOf<UEMPLayerWidget> EMPLayerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP|UI")
	TSubclassOf<UEMPMarkWidget> EMPMarkWidgetClass;

	UFUNCTION(Server, Reliable)
	void TryEMP();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void CacheAbilityData(const FPartnerEMPAbilityData& InAbilityData);

	UFUNCTION(Client, Reliable)
	void ClientStartEMPSearch();

	UFUNCTION(Client, Reliable)
	void ClientStopEMPSearch();

	UFUNCTION(Client, Reliable)
	void ClientCompleteEMP();

	UFUNCTION(Client, Reliable)
	void DefaultWidgetControl(bool InFlag);

	UFUNCTION(Server, Reliable)
	void ServerCompleteEMP(const TArray<AActor*>& InMarkedActors);

	UFUNCTION(Server, Reliable)
	void ServerCancelEMP();

	UFUNCTION(Server, Reliable)
	void ServerExpireEMP();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void RefreshEMPCandidates();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void ClearEMPCandidates();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void StopEMPCandidateSearch();

	void RefocusEMPInput();
	void CancelForReboot();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool IsEMPCandidateSearchActive() const { return bEMPCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool IsEMPInteractionActive() const { return bEMPActive || bEMPCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "EMP")
	const TArray<AActor*>& GetEMPCandidateActors() const { return EMPCandidateActors; }

	UFUNCTION(Server, Reliable)
	void TryMarkEMPTarget(AActor* TargetActor);

	// 위젯에서 타이머 완료 or Tab confirm 시 호출
	bool NotifyEMPConfirmed();
	void NotifyEMPExpired();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool HasMarkedTargets() const { return MarkedActors.Num() > 0; }

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UEMPableComponent>> EMPCandidateComponents;

	UPROPERTY(Transient)
	TObjectPtr<UEMPLayerWidget> EMPLayerWidget;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "EMP")
	FPartnerEMPAbilityData CachedAbilityData;

	TArray<AActor*> EMPCandidateActors;
	TArray<AActor*> MarkedActors;

	UPROPERTY(Replicated)
	uint8 bEMPActive : 1 = false;

	uint8 bEMPCandidateSearchActive : 1 = false;

	int32 LastDebugCandidateCount = INDEX_NONE;

	float EMPStartTimeSeconds = 0.0f;
	FUILayerHandle EMPLayerHandle;

	UEMPableComponent* ResolveEMPableComponent(AActor* Actor) const;
	bool IsCandidateActorValid(AActor* Actor, UEMPableComponent* EMPableComponent, FVector2D& OutScreenLocation) const;
	bool IsActorInViewport(AActor* Actor, FVector2D& OutScreenLocation) const;
	bool HasLineOfSight(AActor* Actor) const;
	void InitializeEMPEarlyCompleteTimer();
	void ResetEMPEarlyCompleteTimer();
	float GetEMPElapsedTime() const;
	void CompleteEMPOnServer(const TArray<AActor*>& InMarkedActors);
	void CancelEMPOnServer();

	void EnsureEMPLayerWidget();
	void DestroyEMPLayerWidget();
	void DestroyRemainingEMPWidgets(APlayerController* PlayerController);
	ULocalPlayerUILayerSubsystem* GetUILayerSubsystem() const;

	void AddEMPCandidate(AActor* Actor, UEMPableComponent* EMPableComponent, const FVector2D& ScreenLocation);
	void RemoveEMPCandidateAt(int32 Index);
	void DeduplicateEMPCandidates();
};
