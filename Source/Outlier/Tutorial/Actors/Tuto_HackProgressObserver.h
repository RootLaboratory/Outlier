#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Tuto_HackProgressObserver.generated.h"

/** 완료된 Source 수가 RequiredCount에 도달하면 EventTag를 한 번 보낸다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FTuto_ProgressCountTrigger
{
	GENERATED_BODY()

	/** 0이면 Sources 전체 수. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (ClampMin = "0"))
	int32 RequiredCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;
};

/**
 * 서버에서 진행 Source(Tuto 해킹 지점) 여러 개를 관찰하고, 완료된 Source 수로 STATE 이벤트를 보낸다.
 * Source는 완료 순간만 delegate로 알리고, 몇 번째 완료가 튜토리얼 순간인지는 이 액터가 결정한다.
 * 수는 delegate 횟수가 아니라 완료 상태인 서로 다른 Source 수로 세므로, 순서와 무관하고 체크포인트로 복원된 Source도 포함한다.
 * 이미 나간 slot은 game-flow 기록이 반복을 막는다. 설정된 slot이 모두 나가면 스스로 파괴한다. Sources와 같은 sublevel에 배치한다.
 */
UCLASS()
class OUTLIER_API ATuto_HackProgressObserver : public AActor, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_HackProgressObserver();

	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return CountTriggers.IsValidIndex(EventIndex) ? CountTriggers[EventIndex].EventTag : FGameplayTag();
	}

	int32 GetCompletedSourceCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** effect 완료를 집계하는 스위치(Tuto_LaserControlSwitch, Tuto_PanelMoverSwitch).
	 *  폭발물은 RequiredCount=1로 첫 폭발, 0으로 전체 폭발을 감지할 수 있다. */
	UPROPERTY(EditInstanceOnly, Category = "Tutorial|Event")
	TArray<TObjectPtr<AActor>> Sources;

	/** 예: 레이저 4곳이면 1(첫 해제), 2(진행), 0(전부). 같은 수에 도달한 slot은 배열 순서로 나간다. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_ProgressCountTrigger> CountTriggers;

private:
	void HandleSourceCompleted(AActor* Source, AOutlierPlayerState* SourcePlayerState);
	int32 GetRequiredCount(int32 Slot) const;
	bool IsSlotFired(int32 Slot) const;
	bool AreAllSlotsFired() const;
	void StopObserving();

	TArray<TWeakObjectPtr<AActor>> ObservedSources;
};
