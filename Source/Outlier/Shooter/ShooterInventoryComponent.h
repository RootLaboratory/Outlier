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

public:
	UShooterInventoryComponent();

	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;
	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;

	void TrySwitchWeapon1();
	void TrySwitchWeapon2();
	void TrySwitchWeapon3();
	void SelectWeaponByIndex(int32 SlotIndex);
	void HandleEquipWeapon(AWeaponBase* Weapon);
};
