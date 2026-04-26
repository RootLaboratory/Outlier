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
		return FirstPersonWeaponSocketRifle;
	case EWeaponType::Pistol:
		return FirstPersonWeaponSocketPistol;
	default:
		return FirstPersonWeaponSocketDefault;
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
		return ThirdPersonWeaponSocketRifle;
	case EWeaponType::Pistol:
		return ThirdPersonWeaponSocketPistol;
	default:
		return ThirdPersonWeaponSocketDefault;
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
		if (OwnedWeapons.IsValidIndex(SlotIndex))
		{
			ShooterCharacter->PlayFirstPersonMontageForWeapon(
				ShooterCharacter->FirstPersonEquipMontage,
				OwnedWeapons[SlotIndex] ? OwnedWeapons[SlotIndex]->GetWeaponType() : EWeaponType::Unarmed);
		}
		ShooterCharacter->ServerSelectWeaponByIndex(SlotIndex);
		return;
	}

	if (!OwnedWeapons.IsValidIndex(SlotIndex))
	{
		return;
	}

	// Inventory가 무기 슬롯을 관리하고, Character에는 최종 장착 결과만 적용시킨다.
	if (ShooterCharacter->CurrentWeapon == OwnedWeapons[SlotIndex])
	{
		return;
	}

	if (ShooterCharacter->IsReloading())
	{
		ShooterCharacter->CancelReloadInternal();
	}

	ShooterCharacter->ResetSecondaryCooldownInternal();
	ShooterCharacter->StopAimInternal();
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(OwnedWeapons[SlotIndex]);
	ShooterCharacter->PlayEquipMontages();
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

	if (!OwnedWeapons.Contains(Weapon))
	{
		OwnedWeapons.Add(Weapon);
	}

	// 실제 장착/해제 라이프사이클은 베이스 캐릭터 구현을 재사용하고,
	// Shooter 쪽에서는 슬롯 목록과 파생 상태만 보정
	// Inventory가 보유 무기와 소켓 규칙을 관리하고, 최종 장착은 Character가 맡음
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(Weapon);
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}
