#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "HackInfoRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FHackInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|UI")
	FText DisplayText;
};
