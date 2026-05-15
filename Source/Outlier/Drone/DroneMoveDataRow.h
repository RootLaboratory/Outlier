#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DroneMoveDataRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FDroneMoveDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BoostSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VerticalSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Acceleration = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Deceleration = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SyncMoveDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SyncMoveInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CameraAssistStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CameraAssistInterpSpeed = 12.0f;
};
