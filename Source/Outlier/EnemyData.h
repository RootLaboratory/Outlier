#pragma once

#include "CoreMinimal.h"
#include "EnemyData.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ID = 0;
};
