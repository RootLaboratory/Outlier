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

	/** 완성형 키 아이콘은 텍스트를 숨긴다. 동적 라벨이 필요한 키(예: F1-F12)에만 켠다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	bool bShowKeyText = false;

	/** Widget 단위. 0이면 공용 표시 높이와 texture 종횡비를 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DataTableImportOptional))
	FVector2D DisplaySize = FVector2D::ZeroVector;
};
