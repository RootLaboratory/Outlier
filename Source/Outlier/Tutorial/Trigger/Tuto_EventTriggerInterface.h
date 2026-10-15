#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Trigger/Tuto_EventTriggerData.h"
#include "UObject/Interface.h"
#include "Tuto_EventTriggerInterface.generated.h"

class AOutlierPlayerState;
class UWorld;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTuto_EventTriggerInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	/**
	 * 서버가 이벤트 하나를 확정하고 연결된 두 플레이어에게 같은 ID를 보낸다.
	 * 각 slot은 PairID와 무관하게 game flow당 한 번 제출된다. 액터의 slot 기록과 서버
	 * SaveSubsystem 기록을 쓰며, 후자는 액터를 다시 생성하는 sublevel 리로드와 체크포인트 복원에도 유지된다.
	 * 클라이언트 DT/UI 수락은 별도로 처리한다. 빈 항목과 반복 slot은 false를 반환한다.
	 */
	virtual bool TriggerEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState);
	/** 정상 완료는 해당 영역의 체인을 진행한다. 기본값은 Upper와 GIF이며 보이스는 계속 재생한다. */
	virtual bool CompleteEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	/** 일치하는 활성/대기 요청을 체인 진행 없이 제거한다. */
	virtual bool CancelEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	/** DT 종료 조건이 Overlap인 일치 요청만 완료한다. */
	virtual bool NotifyEventOverlap(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	virtual FGameplayTag GetEventTag(int32 EventIndex) const = 0;
	bool IsEventSubmitted(int32 EventIndex) const { return SubmittedEventIndices.Contains(EventIndex); }
#if !UE_BUILD_SHIPPING
	/** 디버그 전용: 이 액터의 제출된 slot을 game-flow 기록까지 포함해 다시 발동 가능하게 한다. */
	void ResetSubmittedEventsForDebug();
#endif

	/** 액터 트리거와 에디터/콘솔 요청 경로가 공유하는 서버 transport. */
	static bool DispatchEventTag(UWorld* World, FGameplayTag EventTag, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventOperation Operation, ETuto_EventRegion Region, const FString& Source);

private:
	bool DispatchEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventOperation Operation, ETuto_EventRegion Region);
	TSet<int32> SubmittedEventIndices;
	bool bDispatchingEvent = false;
};
