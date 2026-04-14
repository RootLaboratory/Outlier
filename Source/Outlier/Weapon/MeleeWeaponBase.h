// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponBase.h"
#include "MeleeWeaponBase.generated.h"

/**
 * 
 */
UCLASS(Abstract)
class OUTLIER_API AMeleeWeaponBase : public AWeaponBase
{
	GENERATED_BODY()

public:
	AMeleeWeaponBase();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float AttackRadius = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float AttackAngle = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float HitWindow = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float KnockbackMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	bool bCanHitMultipleTargets = false;

public:
	virtual void PerformAttack() override;
	virtual void TraceMeleeHit();
	virtual void ApplyHitToTarget(AActor* Target);
};
