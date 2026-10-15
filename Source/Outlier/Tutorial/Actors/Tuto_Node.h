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

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	/** Slot 0: 첫 상호작용 시도. Slot 1: 첫 수집 성공. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleNodeInteractionAttempted(AOutlierPlayerState* SourcePlayerState);
	virtual void HandleNodeCollected(AOutlierPlayerState* SourcePlayerState);
};
