#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Actors/Tuto_EventTriggerVolume.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Tuto_PassageTransitionVolume.generated.h"

class APassageHatch;
class AFirstPersonCharacter;
class ULocalPlayerPostProcessSubsystem;

/** One-shot tutorial entry that moves the player pair and requests hatch closure on the server. */
UCLASS()
class OUTLIER_API ATuto_PassageTransitionVolume : public ATuto_EventTriggerVolume
{
	GENERATED_BODY()

public:
	/** Level actor marking the Shooter's capsule center and facing, as with a preset player start.
	 * Leave enough room for both player capsules. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tutorial|Passage")
	TObjectPtr<AActor> TeleportDestination;

	/** Local offset from the Shooter destination; preset loading uses (0, 150, 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Passage", meta = (Units = "cm"))
	FVector PartnerSpawnOffset = FVector(0.0f, 150.0f, 0.0f);

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tutorial|Passage")
	TObjectPtr<APassageHatch> TargetHatch;

	/** Only called through the owning controller RPC after the local screen is covered. */
	void NotifyScreenCovered(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId);

	// Called by the owning controller's RPCs; all local presentation state lives here.
	void StartLocalPassageFade(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId,
		const FOutlierScreenTransitionTiming& Timing);
	void RevealLocalPassage(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId, FVector Destination);
	void CancelLocalPassage(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId);
	static bool IsLocalTransitionActive(const AFirstPersonPlayerController* Controller);

protected:
	virtual bool HandlePawnEntered(AOutlierPlayerState* SourcePlayerState) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandlePassageCovered(FGuid InExecutionId);
	void PollPassageArrival();
	void ClearLocalPassageFade();
	void HandleLocalPawnInvalidated(AFirstPersonCharacter* Character);

	UFUNCTION()
	void HandleLocalPawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION()
	void HandleLocalParticipantEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	UFUNCTION()
	void HandleLocalCharacterDeath();

	TWeakObjectPtr<AFirstPersonPlayerController> LocalPassageController;
	TWeakObjectPtr<APawn> LocalPassagePawn;
	TWeakObjectPtr<ULocalPlayerPostProcessSubsystem> PassagePostProcessSubsystem;
	FGuid LocalPassageId;
	FDelegateHandle PassageCoveredHandle;
	FDelegateHandle PassageRevealedHandle;
	FTimerHandle PassageWatchdogTimer;
	FTimerHandle PassageArrivalTimer;
	FVector PassageArrivalLocation = FVector::ZeroVector;
	FOutlierScreenTransitionTiming LocalPassageTiming;
	bool bPassageCoveredSent = false;
	bool bPassageInputLocked = false;

	bool TeleportPair(AOutlierPlayerState* SourcePlayerState);
	void FinishCoveredTransition();
	void CancelTransition();
	TWeakObjectPtr<AOutlierPlayerState> PendingSource;
	TWeakObjectPtr<AFirstPersonPlayerController> ShooterController;
	TWeakObjectPtr<AFirstPersonPlayerController> PartnerController;
	TWeakObjectPtr<APawn> PendingShooter;
	TWeakObjectPtr<APawn> PendingPartner;
	FOutlierScreenTransitionTiming PassageTiming;
	FGuid ExecutionId;
	FTimerHandle TransitionTimeout;
	FTimerHandle MinimumFadeTimer;
	double FadeStartedAt = 0.0;
	bool bShooterCovered = false;
	bool bPartnerCovered = false;
	bool bTransitionCommitted = false;
};
