#pragma once

#include "CoreMinimal.h"
#include "EnemyStat.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyStat
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ID = 0;
};
