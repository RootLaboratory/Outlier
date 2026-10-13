#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_EventTriggerVolume.generated.h"

class AFirstPersonPlayerController;
class UBoxComponent;
class UOutlierWorldGuideTargetComponent;

/** One location, one EventID. Local entry is notified through the owning controller. */
UCLASS()
class OUTLIER_API ATuto_EventTriggerVolume : public AActor, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_EventTriggerVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;

	virtual FGameplayTag GetEventTag(int32 EventIndex) const override { return EventIndex == 0 ? EventTag : FGameplayTag(); }

	/** Called only by the server controller's entry RPC; the client does not supply EventID or PlayerState. */
	bool ConfirmClientEntry(AFirstPersonPlayerController* Controller);
	void ReceiveEntryResult(AFirstPersonPlayerController* Controller, bool bAccepted);
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostLoad() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|Event")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	virtual bool HandlePawnEntered(AOutlierPlayerState* SourcePlayerState);

private:
	// Serialized old Tuto volume settings are migrated once; no editable array remains.
	UPROPERTY()
	TArray<FTuto_EventTriggerData> EventTriggers_DEPRECATED;

	UPROPERTY(ReplicatedUsing = OnRep_HasTriggered)
	bool bHasTriggered = false;

	UFUNCTION()
	void OnRep_HasTriggered();

	UFUNCTION()
	void HandleTutoBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void RequestLocalEntry(APawn* Pawn);
	TWeakObjectPtr<AFirstPersonPlayerController> PendingController;
	bool bRequestPending = false;
	bool bConfirmingEntry = false;
};
