// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeaponRangeRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponRangeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RangeProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FalloffStartRangeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxRangeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MinDamageMultiplier = 1.0f;
};
