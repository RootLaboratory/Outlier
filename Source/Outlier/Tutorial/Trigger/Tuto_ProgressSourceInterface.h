#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Tuto_ProgressSourceInterface.generated.h"

class AActor;
class AOutlierPlayerState;

/** 서버 전용. 진행 Source 하나가 완료된 순간. 완료시킨 플레이어로 이벤트 수신 Pair를 정한다. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTuto_OnProgressSourceCompleted, AActor* /*Source*/, AOutlierPlayerState* /*SourcePlayerState*/);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTuto_ProgressSourceInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * effect 타깃은 완료를 Switch에 알리고, Switch는 연결된 타깃이 모두 끝나면 Observer에 알린다.
 * WorldGuide는 Switch가 소유하며, 몇 번째 Switch 완료가 TTS 시점인지는 Observer가 결정한다.
 */
class OUTLIER_API ITuto_ProgressSourceInterface
{
	GENERATED_BODY()

public:
	FTuto_OnProgressSourceCompleted OnProgressCompleted;

	/** 체크포인트 복원을 포함한 현재 완료 상태. Observer는 이벤트 횟수가 아니라 이 상태로 수를 센다. */
	virtual bool IsProgressCompleted() const = 0;

	/** effect를 요청한 플레이어. 비동기 이동 후에도 이벤트 수신 Pair를 유지한다. */
	void SetProgressInstigator(AOutlierPlayerState* PlayerState);

protected:
	/** 서버 전용 완료 알림. 마커 소유자인 Switch가 별도로 Manager에 전달한다. */
	void CompleteProgress(AOutlierPlayerState* SourcePlayerState);
	AOutlierPlayerState* GetProgressInstigator() const;

private:
	TWeakObjectPtr<AOutlierPlayerState> ProgressInstigator;
};
