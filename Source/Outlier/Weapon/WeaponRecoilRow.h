// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeaponRecoilRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponRecoilRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RecoilProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float PitchAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float LocationXAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float LocationYAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FovAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RecoverySpeed = 0.0f;
};
