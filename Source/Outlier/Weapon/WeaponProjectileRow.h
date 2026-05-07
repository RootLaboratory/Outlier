// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeaponProjectileRow.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponProjectileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ProjectileProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ProjectileSpeedCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxRangeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float StunTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ReuseCooldown = 0.0f;
};
