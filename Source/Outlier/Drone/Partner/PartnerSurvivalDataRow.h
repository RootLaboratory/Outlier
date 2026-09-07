#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerSurvivalDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerSurvivalDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float RebootTime = 10.0f;
};
