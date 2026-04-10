// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponBase.h"
#include "RangedWeaponBase.generated.h"

/**
 * 
 */
UCLASS(Abstract)
class OUTLIER_API ARangedWeaponBase : public AWeaponBase
{
	GENERATED_BODY()

protected:
	// 1탄창
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 MagazineSize = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 CurrentAmmo = 30;

	// 잔탄
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 ReserveAmmo = 90;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	float ReloadTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	float RecoilMultiplier = 1.0f;

	// Bloom : 탄퍼짐

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomCurrent = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomMin = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomMax = 7.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomPerShot = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomRecoveryRate = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float AimBloomMultiplier = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	uint8 bIsAutomatic : 1 = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	uint8 bIsReloading : 1 = false;

public:
	virtual bool CanAttack() const override;
	virtual void StartAttack() override;
	virtual void StopAttack() override;
	virtual void PerformAttack() override;

	virtual bool CanReload() const;
	virtual void Reload();
	virtual void ConsumeAmmo();
	virtual void FireShot();
	virtual void ApplyRecoil();
	virtual void ApplyBloomPerShot();
	virtual void RecoverBloom(float DeltaTime);
	virtual float GetCurrentSpread() const;

	virtual void SetAiming(bool Aimming);
};
