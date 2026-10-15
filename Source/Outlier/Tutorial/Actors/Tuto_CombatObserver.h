#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Room/RoomCombatSubsystem.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_CombatObserver.generated.h"

class UOutlierWorldGuideTargetComponent;

class UEnemyAdaptationSubsystem;
struct FEnemyAdaptationUpdateResult;

/**
 * 서버에서 전투 방 하나를 관찰한다. 클라이언트는 WorldGuide target anchor만 유지한다. combat/adaptation 서브시스템은 사실만 브로드캐스트하고,
 * 그중 무엇이 튜토리얼 순간인지는 이 액터가 결정한다. 방의 gameplay sublevel에 배치한다. 체크포인트 리로드가
 * 복원된 방 상태로 다시 생성하고, game-flow 기록이 이미 발동한 slot의 반복을 막는다. 방이 클리어되거나,
 * BeginPlay 시 복원된 방이 이미 클리어 상태이거나 설정된 slot 이벤트가 모두 이미 나갔으면 스스로 파괴한다.
 * 증원은 RoomCombat이 알림을 주지 않으므로 전투 중 Wave index를 직접 폴링해 판정한다.
 */
UCLASS()
class OUTLIER_API ATuto_CombatObserver : public AActor, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_CombatObserver();

	/** 이 상속 컴포넌트에서 WidgetID, WidgetClass, 마커 anchor를 설정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

	static constexpr int32 ReinforcementSlot = 0;
	static constexpr int32 ResistanceSlot = 1;
	static constexpr int32 ClearedSlot = 2;

	/** Outlier.Tuto.CombatObserver.Dump용 서버 상태 요약. */
	FString BuildDebugReport() const;
#if !UE_BUILD_SHIPPING
	/** 게임플레이 순간이 발생한 것처럼 slot 핸들러를 실행한다. one-shot 기록은 그대로 적용된다. */
	bool DebugFireSlot(int32 Slot);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slot 0: 전투 중 첫 후속 Wave(증원) 시작. Slot 1: 이 방 전투 중 ResistanceLevel1에 도달한
	 *  첫 총기 킬. Slot 2: 방의 모든 phase 완료. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	/** 각 이벤트 발동 시 이 방 안의 플레이어로부터 pair를 확정한다. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	/** 증원으로 볼 Wave index. INDEX_NONE이면 phase와 무관하게 전투 중 처음 시작되는 Wave 1 이상을 받는다. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (ClampMin = "-1"))
	int32 ReinforcementWaveIndex = INDEX_NONE;

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
	bool IsSlotFired(int32 Slot) const;
	bool AreAllSlotsFired() const;
	void StartWavePolling();
	void StopWavePolling();
	void PollWave();

	static constexpr float WavePollInterval = 0.25f;

	TWeakObjectPtr<URoomCombatSubsystem> ObservedCombat;
	TWeakObjectPtr<UEnemyAdaptationSubsystem> ObservedAdaptation;
	FTimerHandle WavePollTimer;
	int32 LastPolledPhase = INDEX_NONE;
	int32 LastPolledWave = INDEX_NONE;
};
