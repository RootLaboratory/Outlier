#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Actors/Tuto_EventTriggerVolume.h"
#include "Tuto_PassageTransitionVolume.generated.h"

class APassageHatch;

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Passage", meta = (ClampMin = "0", ClampMax = "2", Units = "s"))
	float FadeOutDuration = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Passage", meta = (ClampMin = "0", ClampMax = "2", Units = "s"))
	float FadeInDuration = 0.5f;

	/** Only called through the owning controller RPC after the local screen is covered. */
	void NotifyScreenCovered(AFirstPersonPlayerController* Controller, const FGuid& InExecutionId);

protected:
	virtual bool HandlePawnEntered(AOutlierPlayerState* SourcePlayerState) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool TeleportPair(AOutlierPlayerState* SourcePlayerState);
	void FinishCoveredTransition();
	void CancelTransition();
	TWeakObjectPtr<AOutlierPlayerState> PendingSource;
	TWeakObjectPtr<AFirstPersonPlayerController> ShooterController;
	TWeakObjectPtr<AFirstPersonPlayerController> PartnerController;
	TWeakObjectPtr<APawn> PendingShooter;
	TWeakObjectPtr<APawn> PendingPartner;
	FGuid ExecutionId;
	FTimerHandle TransitionTimeout;
	FTimerHandle MinimumFadeTimer;
	double FadeStartedAt = 0.0;
	bool bShooterCovered = false;
	bool bPartnerCovered = false;
	bool bTransitionCommitted = false;
};
