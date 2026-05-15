// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OutlierCheckpointData.generated.h"

/**
 *
 */
USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierCheckpointData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName LevelName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName CheckpointId = NAME_None;

	bool IsValid() const
	{
		return LevelName != NAME_None && CheckpointId != NAME_None;
	}
};
