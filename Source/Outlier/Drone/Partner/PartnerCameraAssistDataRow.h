#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PartnerCameraAssistDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FPartnerCameraAssistDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistTargetOffset = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector AssistTargetLocalOffset = FVector(0.0f, 0.0f, 50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistMinDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistMaxDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistMaxAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistDeadZoneAngle = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistInterpSpeed = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AssistStrength = 100.0f;
};
