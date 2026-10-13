#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractionNode.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_Node.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_Node : public AInteractionNode, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_Node();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	/** Slot 0: first interaction attempt. Slot 1: first successful collection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleNodeInteractionAttempted(AOutlierPlayerState* SourcePlayerState);
	virtual void HandleNodeCollected(AOutlierPlayerState* SourcePlayerState);
};
