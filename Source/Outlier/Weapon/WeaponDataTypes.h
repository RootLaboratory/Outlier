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

// 무기 타입 -> 슬롯은 1:1이라 슬롯 자체가 무기 정체성의 일부다.
// (원래 ShooterInventoryComponent.h에 있었는데, PlayerState의 로드아웃 스냅샷이
//  인벤토리 컴포넌트 전체를 include하지 않고도 슬롯을 다루려면 여기 있어야 한다.)
UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
	Unarmed		UMETA(DisplayName = "Unarmed"),
	Primary		UMETA(DisplayName = "Primary"),
	Secondary	UMETA(DisplayName = "Secondary"),
	Melee		UMETA(DisplayName = "Melee"),
	Max			UMETA(Hidden)
};
