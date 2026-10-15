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

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slot 0: 무기/연출 적용 후 실제 슈트 획득. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleSuitAcquired(AOutlierPlayerState* SourcePlayerState);

private:
	void HandleAcquiredSuitChanged(AOutlierPlayerState* PlayerState);
	void StopWatchingAcquisition();

	TWeakObjectPtr<AShooterCharacter> WatchedShooter;
	TWeakObjectPtr<AOutlierPlayerState> WatchedPlayerState;
};
