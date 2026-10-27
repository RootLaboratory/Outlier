#pragma once

#include "CoreMinimal.h"
#include "WorldWidgetPresentation.generated.h"

class UWidgetComponent;

/** Local presentation settings. Scale is ReferenceDistance / camera distance, clamped to Min/Max. */
USTRUCT(BlueprintType)
struct OUTLIER_API FWorldWidgetPresentation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget", meta = (ClampMin = "0.01"))
	float MinScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget", meta = (ClampMin = "0.01"))
	float MaxScale = 1.0f;

	/** Distance in cm at which the render scale is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget", meta = (ClampMin = "1.0", Units = "cm"))
	float ReferenceDistance = 1000.0f;

	/** World-space offset in cm, added to the existing anchor/Z offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget", meta = (Units = "cm"))
	FVector WidgetOffset = FVector::ZeroVector;

	void Apply(UWidgetComponent* Component, const FVector& Anchor) const;
};
