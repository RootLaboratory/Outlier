#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "JournalData.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FJournalRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal", meta = (MultiLine = true))
	FText Desc;
};
