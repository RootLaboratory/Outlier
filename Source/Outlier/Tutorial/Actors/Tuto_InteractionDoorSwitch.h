#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableSwitch.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_InteractionDoorSwitch.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_InteractionDoorSwitch : public AInteractableSwitch, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_InteractionDoorSwitch();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	/** Slot 0: first successful interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleSwitchInteracted(AOutlierPlayerState* SourcePlayerState);
};
