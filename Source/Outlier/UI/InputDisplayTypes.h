#pragma once

#include "CoreMinimal.h"
#include "InputDisplayTypes.generated.h"

class UInputAction;
class UTexture2D;

UENUM(BlueprintType)
enum class EInputDisplayKeySelection : uint8
{
	AllMapped,
	FirstMapped
};

/** 표시용 표기일 뿐이며, 이 기호들은 게임플레이 조건을 평가하지 않는다. */
UENUM(BlueprintType)
enum class EInputDisplaySeparator : uint8
{
	None,
	Plus,
	Custom
};

USTRUCT(BlueprintType)
struct OUTLIER_API FInputDisplayTerm
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EInputDisplayKeySelection KeySelection = EInputDisplayKeySelection::AllMapped;

	/** 첫 번째로 표시되는 그룹에서는 무시된다. None은 그룹을 나란히 배치한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EInputDisplaySeparator SeparatorBefore = EInputDisplaySeparator::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (EditCondition = "SeparatorBefore == EInputDisplaySeparator::Custom", EditConditionHides))
	TObjectPtr<UTexture2D> SymbolTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (EditCondition = "SeparatorBefore == EInputDisplaySeparator::Custom", EditConditionHides))
	FText SymbolText;

	/** IA 그룹 전체를 둘러싼 추가 padding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin Padding;
};

/** 역할별 InputDisplay DT에서 작성되어 표시 전용 위젯에 전달된다. */
USTRUCT(BlueprintType)
struct OUTLIER_API FInputDisplayDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TArray<FInputDisplayTerm> Expression;

	/** Manual 튜토리얼용 선택적 완료 액션. 단일 term 정의는 그 유일한 IA가 기본값이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TObjectPtr<UInputAction> CompletionInputAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0"))
	float KeySpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0"))
	float GroupSpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin SymbolPadding = FMargin(8.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D SymbolSize = FVector2D(16.0, 16.0);

	bool IsValid() const;
};
