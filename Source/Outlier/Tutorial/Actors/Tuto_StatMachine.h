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

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. */
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

	/** Slot 0: 잠금 해제 성공. Slot 1: 두 플레이어가 모두 마치고 업그레이드 UI를 실제로 닫음. */
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
