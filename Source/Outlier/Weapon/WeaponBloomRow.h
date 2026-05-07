// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/WeaponDataTypes.h"
#include "WeaponBloomRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponBloomRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BloomProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponAimMode AimMode = EWeaponAimMode::Hip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponMoveState MoveState = EWeaponMoveState::StillOrCrouch;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MinBloom = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxBloom = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float IncPerShot = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RecoveryRate = 5.0f;
};
