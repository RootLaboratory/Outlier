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

	/** Complete key icons hide text. Enable this only for keys needing a dynamic label (e.g. F1-F12). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	bool bShowKeyText = false;

	/** Widget units. Zero uses a common display height and the texture's aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	FVector2D DisplaySize = FVector2D::ZeroVector;
};
