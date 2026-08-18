// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"

UShooterInventoryComponent::UShooterInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShooterInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	const int32 SlotCount = static_cast<int32>(EWeaponSlot::Max);
	if (WeaponSlots.Num() != SlotCount)
	{
		WeaponSlots.SetNum(SlotCount);
	}
}

void UShooterInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UShooterInventoryComponent, WeaponSlots);
	DOREPLIFETIME(UShooterInventoryComponent, CurrentSlot);
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
	SelectWeaponSlot(EWeaponSlot::Primary);
}

void UShooterInventoryComponent::TrySwitchWeapon2()
{
	SelectWeaponSlot(EWeaponSlot::Secondary);
}

void UShooterInventoryComponent::TrySwitchWeapon3()
{
	SelectWeaponSlot(EWeaponSlot::Melee);
}

void UShooterInventoryComponent::SelectWeaponByIndex(int32 SlotIndex)
{
	SelectWeaponSlot(static_cast<EWeaponSlot>(SlotIndex));
}

void UShooterInventoryComponent::HandleEquipWeapon(AWeaponBase* Weapon)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s HandleEquipWeapon Enter Weapon=%s Shooter=%s Authority=%d SlotNum=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter),
		*GetNameSafe(Weapon),
		*GetNameSafe(ShooterCharacter),
		ShooterCharacter && ShooterCharacter->HasAuthority() ? 1 : 0,
		WeaponSlots.Num()
	);

	if (!ShooterCharacter || !Weapon || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (!ShooterCharacter->CanStartAction(EShooterActionLock::Equip))
	{
		return;
	}

	if (!Weapon->CanBePickedUpBy(ShooterCharacter))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s HandleEquipWeapon blocked unavailable Weapon=%s"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*GetNameSafe(Weapon)
		);
		return;
	}

	const EWeaponSlot Slot = GetSlotForWeaponType(Weapon->GetWeaponType());
	const int32 SlotIndex = static_cast<int32>(Slot);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s HandleEquipWeapon Slot WeaponType=%d Slot=%d Valid=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter),
		static_cast<int32>(Weapon->GetWeaponType()),
		SlotIndex,
		WeaponSlots.IsValidIndex(SlotIndex) ? 1 : 0
	);

	if (!IsValidWeaponSlot(Slot))
	{
		return;
	}


	AWeaponBase* OldWeapon = WeaponSlots[SlotIndex];

	if (OldWeapon && OldWeapon != Weapon)
	{
		FTransform DropTransform = Weapon->GetActorTransform();
		DropTransform.SetScale3D(FVector::OneVector);
		OldWeapon->OnDropped(DropTransform, ShooterCharacter);
	}

	WeaponSlots[SlotIndex] = Weapon;
	CurrentSlot = Slot;

	// 실제 장착/해제 라이프사이클은 베이스 캐릭터 구현을 재사용하고,
	// Shooter 쪽에서는 슬롯 목록과 파생 상태만 보정
	// Inventory가 보유 무기와 소켓 규칙을 관리하고, 최종 장착은 Character가 맡음
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(Weapon);
	ShooterCharacter->PlayEquipMontages();
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}

void UShooterInventoryComponent::SelectWeaponSlot(EWeaponSlot Slot)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDead())
	{
		return;
	}

	if (!ShooterCharacter->CanStartAction(EShooterActionLock::Equip))
	{
		return;
	}

	const int32 WeaponIndex = static_cast<int32>(Slot);

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSelectWeaponByIndex(WeaponIndex);
		return;
	}

	if (!IsValidWeaponSlot(Slot))
	{
		return;
	}

	AWeaponBase* TargetWeapon = WeaponSlots[WeaponIndex];
	if (!TargetWeapon || TargetWeapon == ShooterCharacter->CurrentWeapon)
	{
		return;
	}

	if (ShooterCharacter->IsReloading())
	{
		ShooterCharacter->CancelReloadInternal();
	}

	ShooterCharacter->StopAimInternal();

	CurrentSlot = Slot;

	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(TargetWeapon);
	ShooterCharacter->PlayEquipMontages();
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}

void UShooterInventoryComponent::CleanupOwnedWeapons()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	for (AWeaponBase* Weapon : WeaponSlots)
	{
		if (!Weapon)
		{
			continue;
		}

		Weapon->OnOwnerLost();
	}

	WeaponSlots.SetNum(static_cast<int32>(EWeaponSlot::Max));
	for (TObjectPtr<AWeaponBase>& Weapon : WeaponSlots)
	{
		Weapon = nullptr;
	}

	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(nullptr);
	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();
}

EWeaponSlot UShooterInventoryComponent::GetSlotForWeaponType(EWeaponType WeaponType)
{
	switch (WeaponType)
	{
	case EWeaponType::Rifle:
		return EWeaponSlot::Primary;
	case EWeaponType::Pistol:
		return EWeaponSlot::Secondary;
	case EWeaponType::Melee:
		return EWeaponSlot::Melee;
	case EWeaponType::Unarmed:
		return EWeaponSlot::Unarmed;
	}

	return EWeaponSlot::Unarmed;
}

bool UShooterInventoryComponent::IsValidWeaponSlot(EWeaponSlot Slot) const
{
	const int32 Index = static_cast<int32>(Slot);

	return WeaponSlots.IsValidIndex(Index);
}
