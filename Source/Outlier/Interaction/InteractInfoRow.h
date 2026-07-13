#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InteractInfoRow.generated.h"

USTRUCT(BlueprintType)
struct FInteractInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
	FText DisplayText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|UI")
	float Scale = 1.0f;
};
