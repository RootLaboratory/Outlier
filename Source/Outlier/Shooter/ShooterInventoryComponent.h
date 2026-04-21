// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Weapon/WeaponBase.h"
#include "ShooterInventoryComponent.generated.h"

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterInventoryComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TObjectPtr<AWeaponBase>> OwnedWeapons;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketDefault = FName("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketDefault = FName("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName FirstPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_FP");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName ThirdPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_TP");

public:
	UShooterInventoryComponent();

	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;
	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;

	void TrySwitchWeapon1();
	void TrySwitchWeapon2();
	void TrySwitchWeapon3();
	void SelectWeaponByIndex(int32 SlotIndex);
	void HandleEquipWeapon(AWeaponBase* Weapon);

	const TArray<TObjectPtr<AWeaponBase>>& GetOwnedWeapons() const { return OwnedWeapons; }
};
