// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCombatComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"
#include "OutlierPlayerState.h"
#include "TimerManager.h"

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
	case EWeaponType::Melee:
		return FirstPersonWeaponSocketMelee;
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
	case EWeaponType::Melee:
		return ThirdPersonWeaponSocketMelee;
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

bool UShooterInventoryComponent::InitializeDefaultMeleeWeapon(AOutlierPlayerState* PlayerState)
{
	AShooterCharacter* Shooter = GetShooterCharacter();
	if (!Shooter || !Shooter->HasAuthority() || !PlayerState
		|| !PlayerState->IsShooterPlayer() || PlayerState->GetShooterCharacter() != Shooter
		|| !IsValidWeaponSlot(EWeaponSlot::Melee))
	{
		return false;
	}

	const TSubclassOf<AWeaponBase> WeaponClass = Shooter->DefaultMeleeWeaponClass;
	if (AWeaponBase* ExistingMelee = GetWeaponInSlot(EWeaponSlot::Melee))
	{
		// 반복 초기화에서는 장착이나 스냅샷을 다시 건드리지 않는다.
		if (WeaponClass && ExistingMelee->GetClass() == WeaponClass.Get())
		{
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("[DefaultMelee] Occupied slot Shooter=%s Weapon=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(ExistingMelee));
		return false;
	}

	// 저장된 장비는 기본 시작 장비보다 우선한다. 빈 Melee의 이전 저장도 여기서 보충하지 않는다.
	if (!PlayerState->GetLoadoutSnapshot().IsEmpty())
	{
		return false;
	}
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefaultMelee] Class is not configured Shooter=%s"),
			*GetNameSafe(Shooter));
		return false;
	}
	if (WeaponClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefaultMelee] Abstract class rejected Shooter=%s Class=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(WeaponClass.Get()));
		return false;
	}

	AWeaponBase* Weapon = AWeaponBase::SpawnLoadoutWeapon(GetWorld(), WeaponClass, Shooter);
	if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed())
	{
		UE_LOG(LogTemp, Error, TEXT("[DefaultMelee] Spawn failed Shooter=%s Class=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(WeaponClass.Get()));
		return false;
	}
	// BP/DataTable 초기화가 끝난 실제 타입을 검사한 뒤에만 기존 슬롯에 반영한다.
	if (Weapon->GetWeaponType() != EWeaponType::Melee)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefaultMelee] Non-melee class rejected Shooter=%s Class=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(WeaponClass.Get()));
		Weapon->Destroy();
		return false;
	}

	ApplyWeaponToSlot(Weapon, EWeaponSlot::Melee, /*bPlayEquipPresentation=*/false);
	// 원격 Listen 플레이어는 아직 Possess 전일 수 있어 Pawn의 PlayerState에 의존하지 않는다.
	CaptureLoadoutToPlayerState(PlayerState);
	return true;
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
	AWeaponBase* ExistingWeapon = GetWeaponInSlot(Slot);
	if (Slot == EWeaponSlot::Melee && ExistingWeapon && ExistingWeapon != Weapon
		&& ShooterCharacter->DefaultMeleeWeaponClass
		&& ExistingWeapon->GetClass() == ShooterCharacter->DefaultMeleeWeaponClass.Get())
	{
		// 드롭과 장착 부수 효과 전에 거부한다. Interact가 방금 만든 사본만 정리하고 원본은 남긴다.
		UE_LOG(LogTemp, Log, TEXT("[DefaultMelee] Replacement blocked Shooter=%s Weapon=%s"),
			*GetNameSafe(ShooterCharacter), *GetNameSafe(Weapon));
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

	ApplyWeaponToSlot(Weapon, Slot, /*bPlayEquipPresentation=*/true);
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
	CancelPendingWeaponSwitch();

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

	// 암전 commit에서는 장착 Notify 없이 부착/표시를 완료한다. 새 입력은 전환 차단이 계속 막는다.
	ApplyWeaponToSlot(RifleWeapon, Slot, /*bPlayEquipPresentation=*/!ShooterCharacter->IsSuitTransitionBlocked());

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

	if (ShooterCharacter->CurrentWeapon)
	{
		PendingSwitchWeapon = TargetWeapon;
		PendingSwitchSlot = Slot;
		bHasPendingWeaponSwitch = true;
		ShooterCharacter->BeginActionLock(EShooterActionLock::Equip);
		PendingSwitchId = ShooterCharacter->BeginProceduralWeaponSwitch();
		UE_LOG(LogTemp, Log, TEXT("%s [FPWeaponSwitch] Lower old=%s target=%s slot=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter), *GetNameSafe(ShooterCharacter->CurrentWeapon),
			*GetNameSafe(TargetWeapon), static_cast<int32>(Slot));
		GetWorld()->GetTimerManager().SetTimer(
			PendingSwitchTimerHandle, this, &UShooterInventoryComponent::ExpirePendingWeaponSwitch,
			FMath::Max(ShooterCharacter->GetFirstPersonSwitchLowerDuration() + 3.0f, 3.0f), false);
		return;
	}

	ApplyWeaponToSlot(TargetWeapon, Slot, /*bPlayEquipPresentation=*/true);
}

void UShooterInventoryComponent::FinishPendingWeaponSwitch(int32 SwitchId)
{
	if (!bHasPendingWeaponSwitch || SwitchId != PendingSwitchId)
	{
		return;
	}
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	AWeaponBase* TargetWeapon = PendingSwitchWeapon.Get();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority() || !TargetWeapon)
	{
		CancelPendingWeaponSwitch();
		return;
	}

	if (ShooterCharacter->IsDead() || ShooterCharacter->GetActionLock() != EShooterActionLock::Equip ||
		GetWeaponInSlot(PendingSwitchSlot) != TargetWeapon)
	{
		CancelPendingWeaponSwitch();
		return;
	}

	const EWeaponSlot TargetSlot = PendingSwitchSlot;
	GetWorld()->GetTimerManager().ClearTimer(PendingSwitchTimerHandle);
	PendingSwitchWeapon.Reset();
	bHasPendingWeaponSwitch = false;
	UE_LOG(LogTemp, Log, TEXT("%s [FPWeaponSwitch] Swap target=%s slot=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter), *GetNameSafe(TargetWeapon), static_cast<int32>(TargetSlot));
	const EWeaponType PreviousWeaponType = ShooterCharacter->GetWeaponType();
	ApplyWeaponToSlot(TargetWeapon, TargetSlot, /*bPlayEquipPresentation=*/false);
	ShooterCharacter->PlayProceduralSwitchThirdPersonEquip(PreviousWeaponType);
}

void UShooterInventoryComponent::ExpirePendingWeaponSwitch()
{
	CancelPendingWeaponSwitch();
}

void UShooterInventoryComponent::CancelPendingWeaponSwitch()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingSwitchTimerHandle);
	}
	if (!bHasPendingWeaponSwitch)
	{
		return;
	}
	PendingSwitchWeapon.Reset();
	bHasPendingWeaponSwitch = false;
	if (AShooterCharacter* ShooterCharacter = GetShooterCharacter())
	{
		if (ShooterCharacter->HasAuthority() && !ShooterCharacter->IsDead() &&
			ShooterCharacter->GetThirdPersonSwitchMontage())
		{
			ShooterCharacter->MulticastPlayThirdPersonSwitchPhase(
				ShooterCharacter->GetWeaponType(), TEXT("Raise"));
		}
		ShooterCharacter->EndActionLock(EShooterActionLock::Equip);
		ShooterCharacter->ClientCancelProceduralWeaponSwitch(PendingSwitchId);
	}
}

void UShooterInventoryComponent::ApplyWeaponToSlot(
	AWeaponBase* Weapon, EWeaponSlot Slot, bool bPlayEquipPresentation)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !IsValidWeaponSlot(Slot))
	{
		return;
	}

	WeaponSlots[static_cast<int32>(Slot)] = Weapon;
	CurrentSlot = Slot;
	if (bPlayEquipPresentation)
	{
		ShooterCharacter->BeginProceduralEquipRaise(Weapon);
	}

	// 실제 장착/해제 라이프사이클은 베이스 캐릭터 구현을 재사용하고,
	// Shooter 쪽에서는 슬롯 목록과 파생 상태만 보정
	// Inventory가 보유 무기와 소켓 규칙을 관리하고, 최종 장착은 Character가 맡음
	ShooterCharacter->AFirstPersonCharacter::EquipWeapon(Weapon);

	// 1P Equip 몽타주/Attach Notify는 사용하지 않는다. 복원도 같은 부착 경로로 즉시 표시한다.
	if (Weapon)
	{
		Weapon->ShowEquippedPresentation();
	}
	if (bPlayEquipPresentation)
	{
		ShooterCharacter->PlayEquipPresentation();
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
	ApplyWeaponToSlot(Weapon, Slot, /*bPlayEquipPresentation=*/false);

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
	// Snapshot은 값으로 받으므로 기존 Actor를 정리해도 복원 기준은 유지된다.
	// 같은 Pawn에 복원이 반복되어도 이전 망치가 슬롯 밖에 남지 않게 한다.
	CleanupOwnedWeapons();

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

void UShooterInventoryComponent::CaptureLoadoutToPlayerState(AOutlierPlayerState* PlayerState) const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (!PlayerState)
	{
		PlayerState = ShooterCharacter->GetPlayerState<AOutlierPlayerState>();
	}
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
	CancelPendingWeaponSwitch();

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
