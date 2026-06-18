// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "Weapon/WeaponBase.h"
#include "ShooterInventoryComponent.generated.h"

UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
	Unarmed		UMETA(DisplayName = "Unarmed"),
	Primary		UMETA(DisplayName = "Primary"),
	Secondary	UMETA(DisplayName = "Secondary"),
	Melee		UMETA(DisplayName = "Melee"),
	Max			UMETA(Hidden)
};

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterInventoryComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

protected:
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

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TObjectPtr<AWeaponBase>> WeaponSlots;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EWeaponSlot CurrentSlot = EWeaponSlot::Primary;

public:
	UShooterInventoryComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;
	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;

	void TrySwitchWeapon1();
	void TrySwitchWeapon2();
	void TrySwitchWeapon3();
	void SelectWeaponByIndex(int32 SlotIndex);

	void HandleEquipWeapon(AWeaponBase* Weapon);
	void SelectWeaponSlot(EWeaponSlot Slot);

	void CleanupOwnedWeapons();
public:

private:
	static EWeaponSlot GetSlotForWeaponType(EWeaponType WeaponType);
	bool IsValidWeaponSlot(EWeaponSlot Slot) const;
};
