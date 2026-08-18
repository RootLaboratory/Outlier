// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "UI/UILayerTypes.h"
#include "PartnerHackComponent.generated.h"

class UHackableComponent;
class UHackCandidateLayerWidget;
class UHackCandidateMarkerWidget;
class UHackableInfoWidget;
class UHackMiniGameWidget;
class ULocalPlayerUILayerSubsystem;

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerHackAbilityData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float CandidateRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float EffectiveRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float MiniGameTime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	float FailPenaltyTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack")
	bool bRequireLineOfSight = true;
};

UCLASS()
class OUTLIER_API UPartnerHackComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerHackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CandidateHalfAngleDegrees = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	uint8 bRequireLineOfSight : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Debug")
	uint8 bDebugHack : 1 = true;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer RequiredCandidateTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer BlockedCandidateTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateLayerWidget> CandidateLayerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateMarkerWidget> CandidateMarkerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackableInfoWidget> HackableInfoWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackMiniGameWidget> HackMiniGameWidgetClass;

	UFUNCTION(Server, Reliable)
	void TryHack();

	void EndHackHold();
	void CancelForReboot();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CacheAbilityData(const FPartnerHackAbilityData& InAbilityData);

	UFUNCTION(Client, Reliable)
	void ClientStartCandidateSearch();
	UFUNCTION(Client, Reliable)
	void ClientStopCandidateSearch();

	UFUNCTION(Client, Reliable)
	void ClientStartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent);

	UFUNCTION(Client, Reliable)
	void ClientStopHackMiniGame();

	UFUNCTION(Client, Reliable)
	void ClientAbortHackForInvalidTarget();

	UFUNCTION(Client, Reliable)
	void DefaultWidgetControl(bool InFlag);

	UFUNCTION(Server, Reliable)
	void ServerCompleteHack(const FHackResultContext& ResultContext);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void RefreshHackCandidates();

	UFUNCTION(Server, Reliable)
	void ServerTryStartHack(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void ClientCompleteHack();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void ClearHackCandidates();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void StopHackCandidateSearch();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackCandidateSearchActive() const { return bHackCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackInteractionActive() const { return bHackCandidateSearchActive || ActiveHackableComponent || HackMiniGameWidget; }

	bool TryBeginHackHold();
	void NotifyHackMarkerHovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor);
	void NotifyHackMarkerUnhovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor);
	void NotifyHackHoldCompleted(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor);
	void ResetLocalHackHoldProgress();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	const TArray<AActor*>& GetHackCandidateActors() const { return HackCandidateActors; }

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHackableComponent>> HackCandidateComponents;

	UPROPERTY(Transient)
	TObjectPtr<UHackableComponent> ActiveHackableComponent;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateLayerWidget> CandidateLayerWidget;

	UPROPERTY(Transient)
	TObjectPtr<UHackMiniGameWidget> HackMiniGameWidget;


private:
	UPROPERTY(VisibleInstanceOnly, Category = "Hack")
	FPartnerHackAbilityData CachedAbilityData;

	TArray<AActor*> HackCandidateActors;

	uint8 bHackCandidateSearchActive : 1 = false;
	int32 LastDebugCandidateCount = INDEX_NONE;
	FUILayerHandle HackCandidateLayerHandle;
	FUILayerHandle HackMiniGameLayerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AActor> HoveredHackActor;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateMarkerWidget> HoveredMarkerWidget;

	FHackQueryContext BuildQueryContext() const;
	UHackableComponent* ResolveHackableComponent(AActor* Actor) const;
	bool IsCandidateActorValid(AActor* Actor, UHackableComponent* HackableComponent, const FHackQueryContext& Context, FVector2D& OutScreenLocation) const;
	bool IsInsidePartnerFrustum(AActor* Actor, FVector2D& OutScreenLocation) const;
	bool HasLineOfSight(AActor* Actor) const;
	void EnsureCandidateLayerWidget();
	void DestroyCandidateLayerWidget();
	bool EnsureHackMiniGameWidget(AActor* TargetActor, UHackableComponent* HackableComponent);
	void DestroyHackMiniGameWidget();
	ULocalPlayerUILayerSubsystem* GetUILayerSubsystem() const;
	void AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation);
	void RemoveHackCandidateAt(int32 Index);
	void DeactivateUnselectedCandidates(UHackableComponent* SelectedComponent);
	void CancelActiveHack();
	void CompleteActiveHack(const FHackResultContext& ResultContext, bool bNotifyClient);
	void SetActiveHackableComponent(UHackableComponent* HackableComponent);
	void ClearActiveHackableComponent();
	void AbortLocalHackForInvalidTarget();
	void HandleHackTargetInvalidated(
		UHackableComponent* InvalidatedComponent,
		EEndPlayReason::Type EndPlayReason);

	void StartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent);

	UFUNCTION()
	void HandleHackMiniGameFinished(const FHackResultContext& ResultContext);
};
