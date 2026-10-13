#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Room/RoomCombatSubsystem.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_CombatObserver.generated.h"

class UOutlierWorldGuideTargetComponent;

class UEnemyAdaptationSubsystem;
struct FEnemyAdaptationUpdateResult;

/**
 * Observes one combat room on the server. Clients retain its WorldGuide target anchor. The combat/adaptation subsystems only broadcast facts; this actor decides
 * which of them are tutorial moments. Place it in the arena persistent level, not a gameplay sublevel, so checkpoint
 * reloads keep its once-per-pair record. It stops observing and destroys itself once the room is cleared.
 */
UCLASS()
class OUTLIER_API ATuto_CombatObserver : public AInfo, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_CombatObserver();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

	static constexpr int32 ReinforcementSlot = 0;
	static constexpr int32 ResistanceSlot = 1;
	static constexpr int32 ClearedSlot = 2;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slot 0: first Automatic phase (reinforcement), before it spawns. Slot 1: first gun kill reaching ResistanceLevel1
	 *  during this room's combat. Slot 2: every phase of the room completed. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	/** The pair is resolved from a player inside this room when each event fires. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	virtual void HandleReinforcement(AOutlierPlayerState* SourcePlayerState);
	virtual void HandleFirstResistance(AOutlierPlayerState* SourcePlayerState);
	virtual void HandleRoomCleared(AOutlierPlayerState* SourcePlayerState);

private:
	UFUNCTION()
	void HandleCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event, int32 CombatPhaseIndex,
		int32 GameplayGeneration);
	void HandleAdaptationUpdated(const FEnemyAdaptationUpdateResult& Result);
	AOutlierPlayerState* FindPlayerInRoom() const;
	void StopObserving();

	TWeakObjectPtr<URoomCombatSubsystem> ObservedCombat;
	TWeakObjectPtr<UEnemyAdaptationSubsystem> ObservedAdaptation;
};
