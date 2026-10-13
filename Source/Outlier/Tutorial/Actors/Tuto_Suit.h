#pragma once

#include "CoreMinimal.h"
#include "Interaction/SuitInteraction.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_Suit.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_Suit : public ASuitInteraction, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_Suit();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slot 0: actual suit acquisition, after weapons/presentation are applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleSuitAcquired(AOutlierPlayerState* SourcePlayerState);

private:
	void HandleAcquiredSuitChanged(AOutlierPlayerState* PlayerState);
	void StopWatchingAcquisition();

	TWeakObjectPtr<AShooterCharacter> WatchedShooter;
	TWeakObjectPtr<AOutlierPlayerState> WatchedPlayerState;
};
