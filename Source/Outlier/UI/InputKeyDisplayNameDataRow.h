#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputKeyDisplayNameDataRow.generated.h"

/** Row Name은 엔진 키 이름이다. 예: LeftShift, PageDown, NumPadZero. 행이 없으면 엔진 표시 이름을 사용한다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FInputKeyDisplayNameDataRow : public FTableRowBase
{
	GENERATED_BODY()

	/** HUD에 표시할 키 라벨. 예: LShift, PgDn, ↑. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	FText DisplayName;
};
