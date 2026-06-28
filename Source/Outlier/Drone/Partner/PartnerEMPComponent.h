#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "GameplayTagContainer.h"
#include "PartnerEMPComponent.generated.h"

class UEMPableComponent;
class UEMPLayerWidget;
class UEMPMarkWidget;

UCLASS()
class OUTLIER_API UPartnerEMPComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerEMPComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	float EMPRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Marking")
	float EMPMarkingTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	FGameplayTagContainer RequiredEMPTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Candidate")
	FGameplayTagContainer BlockedEMPTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EMP|Debug")
	uint8 bDebugEMP : 1 = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP|UI")
	TSubclassOf<UEMPLayerWidget> EMPLayerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP|UI")
	TSubclassOf<UEMPMarkWidget> EMPMarkWidgetClass;

	UFUNCTION(Server, Reliable)
	void TryEMP();

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

	UFUNCTION(NetMulticast, Reliable)
	void MulticastTriggerEMPEffect(AActor* TargetActor);

	UFUNCTION(Server, Reliable)
	void ServerCancelEMP();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void RefreshEMPCandidates();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void ClearEMPCandidates();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void StopEMPCandidateSearch();

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool IsEMPCandidateSearchActive() const { return bEMPCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "EMP")
	const TArray<AActor*>& GetEMPCandidateActors() const { return EMPCandidateActors; }

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
	TArray<AActor*> EMPCandidateActors;
	TArray<AActor*> MarkedActors;

	uint8 bEMPActive : 1 = false;
	uint8 bEMPCandidateSearchActive : 1 = false;

	int32 LastDebugCandidateCount = INDEX_NONE;

	UEMPableComponent* ResolveEMPableComponent(AActor* Actor) const;
	bool IsCandidateActorValid(AActor* Actor, UEMPableComponent* EMPableComponent, FVector2D& OutScreenLocation) const;
	bool IsActorInViewport(AActor* Actor, FVector2D& OutScreenLocation) const;
	bool HasLineOfSight(AActor* Actor) const;

	void EnsureEMPLayerWidget();
	void DestroyEMPLayerWidget();
	void DestroyRemainingEMPWidgets(APlayerController* PlayerController);
	void ApplyEMPInputMode();
	void RestoreGameInputMode();

	void AddEMPCandidate(AActor* Actor, UEMPableComponent* EMPableComponent, const FVector2D& ScreenLocation);
	void RemoveEMPCandidateAt(int32 Index);
	void DeduplicateEMPCandidates();
};
