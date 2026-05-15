// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "WeaponDataTypes.generated.h"

UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto,
	FullAuto
};

UENUM(BlueprintType)
enum class EWeaponFireType : uint8
{
	HitScan,
	Projectile,
	Melee
};

UENUM(BlueprintType)
enum class EWeaponAimMode : uint8
{
	Hip,
	ADS,
	Any
};

UENUM(BlueprintType)
enum class EWeaponMoveState : uint8
{
	StillOrCrouch,
	Moving,
	Air
};
