#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OutlierVitalityDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierVitalityDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitality", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.0f;
};
