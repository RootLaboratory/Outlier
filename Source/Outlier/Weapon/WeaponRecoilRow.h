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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control")
	float ControlPitchAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera")
	float CameraLocationXAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera")
	float CameraLocationYAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera")
	float CameraFovKickAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control")
	float ControlRecoverySpeed = 0.0f;
};
