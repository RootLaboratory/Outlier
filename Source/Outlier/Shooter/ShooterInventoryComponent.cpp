// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"
#include "OutlierPlayerState.h"

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

AWeaponBase* UShooterInventoryComponent::GetWeaponInSlot(EWeaponSlot Slot) const
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	return WeaponSlots.IsValidIndex(SlotIndex) ? WeaponSlots[SlotIndex] : nullptr;
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
	if (Slot != EWeaponSlot::Primary)
	{
		ShooterCharacter->EndActiveWeaponOvercharge(true);
	}


	AWeaponBase* OldWeapon = WeaponSlots[SlotIndex];

	if (OldWeapon && OldWeapon != Weapon)
	{
		FTransform DropTransform = Weapon->GetActorTransform();
		DropTransform.SetScale3D(FVector::OneVector);
		OldWeapon->OnDropped(DropTransform, ShooterCharacter);
	}

	ApplyWeaponToSlot(Weapon, Slot, /*bPlayEquipMontage=*/true);
}

bool UShooterInventoryComponent::EquipSuitRifle(AWeaponBase* RifleWeapon)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	const EWeaponSlot Slot = EWeaponSlot::Primary;
	const int32 SlotIndex = static_cast<int32>(Slot);

	if (!ShooterCharacter
		|| !ShooterCharacter->HasAuthority()
		|| !RifleWeapon
		|| RifleWeapon->GetWeaponType() != EWeaponType::Rifle
		|| !IsValidWeaponSlot(Slot)
		|| !RifleWeapon->CanBePickedUpBy(ShooterCharacter))
	{
		return false;
	}

	if (ShooterCharacter->CombatComponent)
	{
		ShooterCharacter->CombatComponent->CancelMeleeAttack();
	}
	if (ShooterCharacter->IsReloading())
	{
		ShooterCharacter->CancelReloadInternal();
	}
	if (ShooterCharacter->IsWeaponOvercharged())
	{
		ShooterCharacter->EndActiveWeaponOvercharge(true);
	}
	ShooterCharacter->StopAimInternal();

	AWeaponBase* PreviousPrimaryWeapon = WeaponSlots[SlotIndex];
	if (PreviousPrimaryWeapon && PreviousPrimaryWeapon != RifleWeapon)
	{
		if (ShooterCharacter->CurrentWeapon == PreviousPrimaryWeapon)
		{
			ShooterCharacter->AFirstPersonCharacter::EquipWeapon(nullptr);
		}

		WeaponSlots[SlotIndex] = nullptr;
		PreviousPrimaryWeapon->OnOwnerLost();
	}

	ApplyWeaponToSlot(RifleWeapon, Slot, /*bPlayEquipMontage=*/true);

	return ShooterCharacter->CurrentWeapon == RifleWeapon;
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

	// 교체할 무기가 없더라도 유효한 슬롯 입력은 현재 근접 공격을 취소한다.
	if (ShooterCharacter->CombatComponent)
	{
		ShooterCharacter->CombatComponent->CancelMeleeAttack();
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
	if (Slot != EWeaponSlot::Primary)
	{
		ShooterCharacter->EndActiveWeaponOvercharge(true);
	}

	ShooterCharacter->StopAimInternal();

	ApplyWeaponToSlot(TargetWeapon, Slot, /*bPlayEquipMontage=*/true);
}

void UShooterInventoryComponent::ApplyWeaponToSlot(
	AWeaponBase* Weapon, EWeaponSlot Slot, bool bPlayEquipMontage)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !IsValidWeaponSlot(Slot))
	{
		return;
	}

	WeaponSlots[static_cast<int32>(Slot)] = Weapon;
	CurrentSlot = Slot;

	// 실제 장착/해제 라이프사이클은 베이스 캐릭터 구현을 재사용하고,
	// Shooter 쪽에서는 슬롯 목록과 파생 상태만 보정
	// Inventory가 보유 무기와 소켓 규칙을 관리하고, 최종 장착은 Character가 맡음
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(Weapon);

	if (bPlayEquipMontage)
	{
		ShooterCharacter->PlayEquipMontages();
	}
	else if (Weapon)
	{
		// OnEquipped 가 1P/3P/Shadow 메시를 전부 숨겨두고 공개는 equip 몽타주 Notify 가 한다.
		// 몽타주를 생략하는 경로(복원)에서는 아무도 다시 보여주지 않으므로 직접 켠다.
		// ASuitInteraction 이 Partner 무기에 대해 이미 같은 처리를 한다.
		Weapon->ShowEquippedPresentation();
	}

	ShooterCharacter->RefreshWeaponMode();
	ShooterCharacter->RefreshCombatState();

	CaptureLoadoutToPlayerState();
}

void UShooterInventoryComponent::RestoreWeaponIntoSlot(
	const FOutlierWeaponSnapshot& WeaponSnapshot,
	EWeaponSlot Slot,
	bool bRestoreAmmo)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!WeaponSnapshot.WeaponClass || !ShooterCharacter || !IsValidWeaponSlot(Slot))
	{
		return;
	}

	AWeaponBase* Weapon = AWeaponBase::SpawnLoadoutWeapon(
		GetWorld(), WeaponSnapshot.WeaponClass, ShooterCharacter);
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("%s RestoreWeaponIntoSlot failed to spawn Class=%s Slot=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*GetNameSafe(WeaponSnapshot.WeaponClass.Get()),
			static_cast<int32>(Slot));
		return;
	}
	ApplyWeaponToSlot(Weapon, Slot, /*bPlayEquipMontage=*/false);

	// OnEquipped가 최초 DataTable 초기화를 수행하면서 탄창을 기본값으로 채울 수 있다.
	// 저장 탄약은 장착 라이프사이클이 끝난 뒤 적용해야 초기화에 덮어써지지 않는다.
	if (bRestoreAmmo && WeaponSnapshot.CurrentAmmo != INDEX_NONE)
	{
		if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(Weapon))
		{
			RangedWeapon->RestoreCheckpointAmmo(WeaponSnapshot.CurrentAmmo);
		}
	}
}

void UShooterInventoryComponent::RestoreLoadout(
	FOutlierLoadoutSnapshot Snapshot,
	bool bRestoreAmmo)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	const int32 CurrentSlotIndex = static_cast<int32>(Snapshot.CurrentSlot);

	// CurrentSlot 을 마지막에 넣는다.
	// AFirstPersonCharacter::EquipWeapon 이 직전 CurrentWeapon 에 OnUnequipped() 를 부르므로,
	// 순서대로 넣기만 하면 앞의 것들은 저절로 스토우되고 마지막 것만 장착 상태로 남는다.
	// 별도의 "슬롯에만 넣기" 경로를 만들 필요가 없다.
	for (int32 SlotIndex = 0; SlotIndex < Snapshot.SlotSnapshots.Num(); ++SlotIndex)
	{
		if (SlotIndex == CurrentSlotIndex)
		{
			continue;
		}

		RestoreWeaponIntoSlot(
			Snapshot.SlotSnapshots[SlotIndex],
			static_cast<EWeaponSlot>(SlotIndex),
			bRestoreAmmo);
	}

	if (Snapshot.SlotSnapshots.IsValidIndex(CurrentSlotIndex))
	{
		RestoreWeaponIntoSlot(
			Snapshot.SlotSnapshots[CurrentSlotIndex],
			Snapshot.CurrentSlot,
			bRestoreAmmo);
	}
}

void UShooterInventoryComponent::BuildLoadoutSnapshot(
	FOutlierLoadoutSnapshot& OutSnapshot,
	bool bCaptureAmmo) const
{
	OutSnapshot.SlotSnapshots.Reset();
	OutSnapshot.SlotSnapshots.SetNum(WeaponSlots.Num());
	for (int32 SlotIndex = 0; SlotIndex < WeaponSlots.Num(); ++SlotIndex)
	{
		const AWeaponBase* Weapon = WeaponSlots[SlotIndex];
		FOutlierWeaponSnapshot& WeaponSnapshot = OutSnapshot.SlotSnapshots[SlotIndex];
		WeaponSnapshot.WeaponClass = Weapon ? Weapon->GetClass() : nullptr;
		WeaponSnapshot.CurrentAmmo = INDEX_NONE;

		if (bCaptureAmmo)
		{
			if (const ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(Weapon))
			{
				WeaponSnapshot.CurrentAmmo = RangedWeapon->GetCurrentAmmo();
			}
		}
	}
	OutSnapshot.CurrentSlot = CurrentSlot;
}

void UShooterInventoryComponent::CaptureLoadoutToPlayerState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	AOutlierPlayerState* PlayerState = ShooterCharacter->GetPlayerState<AOutlierPlayerState>();
	if (!PlayerState)
	{
		return;
	}

	// PartnerWeaponClass 는 인벤토리 소관이 아니다 (슈트가 직접 지급한다).
	// 빈 구조체로 시작하면 Shooter 무기를 바꿀 때마다 Partner 기록이 지워지므로
	// 기존 스냅샷을 읽어와 Shooter 쪽만 갱신한다.
	FOutlierLoadoutSnapshot Snapshot = PlayerState->GetLoadoutSnapshot();

	BuildLoadoutSnapshot(Snapshot, /*bCaptureAmmo=*/false);

	PlayerState->SetLoadoutSnapshot(Snapshot);
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
