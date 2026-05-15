#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DroneControlDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FDroneControlDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float LookSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float PitchMin = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float PitchMax = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float LookInputDeadZone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float TurnInterpSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RotationLagAmount = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RotationLagRecoverSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CameraRollOnTurn = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CameraRollInterpSpeed = 8.0f;
};
