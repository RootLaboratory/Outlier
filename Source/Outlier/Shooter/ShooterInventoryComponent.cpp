// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/ShooterCharacter.h"

UShooterInventoryComponent::UShooterInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FName UShooterInventoryComponent::GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return NAME_None;
	}

	switch (WeaponType)
	{
	case EWeaponType::Rifle:
		return ShooterCharacter->FirstPersonWeaponSocketRifle;
	case EWeaponType::Pistol:
		return ShooterCharacter->FirstPersonWeaponSocketPistol;
	default:
		return ShooterCharacter->FirstPersonWeaponSocketDefault;
	}
}

FName UShooterInventoryComponent::GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return NAME_None;
	}

	switch (WeaponType)
	{
	case EWeaponType::Rifle:
		return ShooterCharacter->ThirdPersonWeaponSocketRifle;
	case EWeaponType::Pistol:
		return ShooterCharacter->ThirdPersonWeaponSocketPistol;
	default:
		return ShooterCharacter->ThirdPersonWeaponSocketDefault;
	}
}

void UShooterInventoryComponent::TrySwitchWeapon1()
{
	SelectWeaponByIndex(0);
}

void UShooterInventoryComponent::TrySwitchWeapon2()
{
	SelectWeaponByIndex(1);
}

void UShooterInventoryComponent::TrySwitchWeapon3()
{
	SelectWeaponByIndex(2);
}

void UShooterInventoryComponent::SelectWeaponByIndex(int32 SlotIndex)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->bIsDead)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		// 슬롯 선택은 서버가 현재 장착 무기를 최종 결정
		ShooterCharacter->ServerSelectWeaponByIndex(SlotIndex);
		return;
	}

	if (!ShooterCharacter->OwnedWeapons.IsValidIndex(SlotIndex))
	{
		return;
	}

	if (ShooterCharacter->CurrentWeapon == ShooterCharacter->OwnedWeapons[SlotIndex])
	{
		return;
	}

	if (ShooterCharacter->bIsReloading)
	{
		ShooterCharacter->CancelReloadInternal();
	}

	ShooterCharacter->GetWorldTimerManager().ClearTimer(ShooterCharacter->SecondaryCooldownStateTimerHandle);
	ShooterCharacter->bSecondaryOnCooldown = false;
	ShooterCharacter->StopAimInternal();
	ShooterCharacter->EquipWeapon(ShooterCharacter->OwnedWeapons[SlotIndex]);
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}

void UShooterInventoryComponent::HandleEquipWeapon(AWeaponBase* Weapon)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !Weapon)
	{
		return;
	}

	if (!ShooterCharacter->OwnedWeapons.Contains(Weapon))
	{
		ShooterCharacter->OwnedWeapons.Add(Weapon);
	}

	// 실제 장착/해제 라이프사이클은 베이스 캐릭터 구현을 재사용하고,
	// Shooter 쪽에서는 슬롯 목록과 파생 상태만 보정
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(Weapon);
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}
