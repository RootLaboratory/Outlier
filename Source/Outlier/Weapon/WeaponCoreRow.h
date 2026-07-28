// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/WeaponDataTypes.h"
#include "WeaponCoreRow.generated.h"

class UWeaponFeedbackDefinition;
class UProceduralAnimValues;

/**
 * 
 */
USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponCoreRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName WeaponId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponType WeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponFireType FireType = EWeaponFireType::HitScan;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Damage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MagazineSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bInfiniteAmmo = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FireRateRpm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BurstShotCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float PostBurstCooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DefaultMinBloom = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DefaultMaxBloom = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float GameplayRecoilMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float GameplayMovementSpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RangeProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BloomProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RecoilProfileId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ProjectileProfileId;
};
