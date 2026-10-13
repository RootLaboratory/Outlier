#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractionStatMachine.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_StatMachine.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_StatMachine : public AInteractionStatMachine, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_StatMachine();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slot 0: successful unlock. Slot 1: both players finish and actually close the upgrade UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleMachineUnlocked(AOutlierPlayerState* SourcePlayerState);
	virtual void HandleMachineUICompleted(AOutlierPlayerState* SourcePlayerState);

private:
	struct FCompletionSession
	{
		TWeakObjectPtr<AOutlierPlayerState> Shooter;
		TWeakObjectPtr<AOutlierPlayerState> Partner;
		uint32 GameplayGeneration = 0;
		bool bInteractionAccepted = false;
		bool bShooterCompleted = false;
		bool bPartnerCompleted = false;
	};

	void WatchPlayerState(AOutlierPlayerState* PlayerState);
	void HandleStatAllocatorUICompleted(AOutlierPlayerState* PlayerState, uint32 GameplayGeneration);
	void TryCompleteSession(int32 PairId);
	TMap<int32, FCompletionSession> CompletionSessions;
	TArray<TWeakObjectPtr<AOutlierPlayerState>> WatchedPlayerStates;
};
