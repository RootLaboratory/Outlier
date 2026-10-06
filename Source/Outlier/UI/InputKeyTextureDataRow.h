#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputKeyTextureDataRow.generated.h"

class UTexture2D;

/** Row Name is the engine key name, e.g. LeftMouseButton or MouseScrollUp. */
USTRUCT(BlueprintType)
struct OUTLIER_API FInputKeyTextureDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UTexture2D> Texture = nullptr;
};
