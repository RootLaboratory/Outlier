#pragma once

#include "CoreMinimal.h"
#include "OutlierArenaInstance.generated.h"

class ULevelStreamingDynamic;
class UWorld;

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierArenaInstance
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ArenaId = INDEX_NONE;

	// StreamingLevel이 없는 아레나(Dedicated Worker: 1프로세스 1아레나라 Persistent World 자체가
	// 아레나)의 소유 World. 등록 시점에 한 번 박고 안 바꾼다.
	// 스트리밍 인스턴스에서는 비어 있다 — 그쪽 소유 World는 로드 상태에 따라 변하므로 캐시하지 않고
	// UOutlierArenaPoolSubsystem::GetArenaWorld()가 매번 계산한다. 직접 읽지 말고 그 함수를 쓸 것.
	// 예전에는 이 사실이 데이터가 아니라 함수 5곳의 조건문(ArenaId==0 && IsPersistentArenaWorld())
	// 으로 흩어져 있었다. 배열에 항목으로 넣으면 "0번은 특별하다"가 아니라 "항목이 하나"가 된다.
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
