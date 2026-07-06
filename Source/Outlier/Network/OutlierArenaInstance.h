#pragma once

#include "CoreMinimal.h"
#include "OutlierArenaInstance.generated.h"

class ULevelStreamingDynamic;

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierArenaInstance
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ArenaId = INDEX_NONE;

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
