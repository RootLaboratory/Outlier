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

/** Display notation only; these symbols do not evaluate gameplay conditions. */
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

	/** Ignored for the first displayed group. None places groups side by side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EInputDisplaySeparator SeparatorBefore = EInputDisplaySeparator::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (EditCondition = "SeparatorBefore == EInputDisplaySeparator::Custom", EditConditionHides))
	TObjectPtr<UTexture2D> SymbolTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (EditCondition = "SeparatorBefore == EInputDisplaySeparator::Custom", EditConditionHides))
	FText SymbolText;

	/** Additional padding around the entire IA group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin Padding;
};

/** Authored in the role's InputDisplay DT and passed to a display-only widget. */
USTRUCT(BlueprintType)
struct OUTLIER_API FInputDisplayDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TArray<FInputDisplayTerm> Expression;

	/** Optional completion action for Manual tutorials. Single-term definitions default to their only IA. */
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
