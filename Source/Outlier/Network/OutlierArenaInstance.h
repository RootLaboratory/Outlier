#pragma once

#include "CoreMinimal.h"
#include "OutlierArenaInstance.generated.h"

class ULevelStreamingDynamic;
class UWorld;

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierArenaInstance
{
	GENERATED_BODY()

	// StreamingLevel이 없는 아레나(Dedicated Worker: 1프로세스 1아레나라 Persistent World 자체가
	// 아레나)의 소유 World. 등록 시점에 한 번 박고 안 바꾼다.
	// 스트리밍 인스턴스에서는 비어 있다 — 그쪽 소유 World는 로드 상태에 따라 변하므로 캐시하지 않고
	// UOutlierArenaSubsystem::GetArenaWorld()가 매번 계산한다. 직접 읽지 말고 그 함수를 쓸 것.
	UPROPERTY()
	TWeakObjectPtr<UWorld> ArenaWorld;

	// Dedicated Worker의 Persistent Arena에서는 null. 스트리밍할 대상이 없다는 뜻이다.
	UPROPERTY()
	TObjectPtr<ULevelStreamingDynamic> StreamingLevel;

	UPROPERTY()
	FTransform InstanceTransform;

	UPROPERTY()
	uint8 bInUse : 1 = false;

	UPROPERTY()
	uint8 bReady : 1 = false;

	UPROPERTY()
	int32 PairId = INDEX_NONE;
};
