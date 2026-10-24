#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputKeyTextureDataRow.generated.h"

class UTexture2D;

/** Row Name은 엔진 키 이름이다. 예: LeftMouseButton, MouseScrollUp. */
USTRUCT(BlueprintType)
struct OUTLIER_API FInputKeyTextureDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UTexture2D> Texture = nullptr;

	/** 켜면 Texture를 프레임으로 사용하고 키 라벨을 겹쳐 표시한다. 끄면 완성형 아이콘만 표시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	bool bShowKeyText = false;

	/** Widget 단위. 0이면 공용 표시 높이와 texture 종횡비를 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	FVector2D DisplaySize = FVector2D::ZeroVector;
};
