#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PresetNodeProvideRow.generated.h"

// Row Name : Level1 / Level2 / Level3 / Level4
USTRUCT(BlueprintType)
struct OUTLIER_API FPresetNodeProvideRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade", meta = (ClampMin = "0"))
	int32 Count = 0;
};
