// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "Shooter/ShooterMovementComponent.h"
#include "OutlierNetUtils.h"

UShooterCombatComponent::UShooterCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UShooterCombatComponent::TryReload()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryReload CurrentWeapon=%s"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), *GetNameSafe(ShooterCharacter->CurrentWeapon));

	if (!ShooterCharacter->HasAuthority())
	{
		// 1인칭 반응성은 로컬에서 먼저 주고, 실제 상태 전이는 서버가 확정
		ShooterCharacter->PlayLocalActionMontage(ShooterCharacter->ReloadMontage);
		ShooterCharacter->ServerReload();
		return;
	}

	RefreshWeaponMode();
	if (!CanReloadInCurrentState())
	{
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
	if (!RangedWeapon || !RangedWeapon->CanReload())
	{
		return;
	}

	if (ShooterCharacter->MovementState == EMovementState::Run)
	{
		ShooterCharacter->StopSprintInternal();
	}

	if (ShooterCharacter->CombatState == ECombatState::Aim)
	{
		StopAimInternal();
	}

	RangedWeapon->BeginReload();
	if (!RangedWeapon->IsReloading())
	{
		return;
	}

	BeginReloadInternal();
}

void UShooterCombatComponent::TryStartAim()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetAimState(true);
	}

	RefreshWeaponMode();
	if (!CanAimInCurrentState())
	{
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
	if (!RangedWeapon)
	{
		return;
	}

	if (ShooterCharacter->MovementState == EMovementState::Run)
	{
		ShooterCharacter->StopSprintInternal();
	}

	ShooterCharacter->bAimHeld = true;
	ShooterCharacter->bIsAiming = true;
	ShooterCharacter->CombatState = ECombatState::Aim;
	RangedWeapon->SetAiming(true);
}

void UShooterCombatComponent::TryStopAim()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetAimState(false);
	}

	StopAimInternal();
	RefreshCombatState();
}

void UShooterCombatComponent::TryStartAttack()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStartAttack CurrentWeapon=%s"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), *GetNameSafe(ShooterCharacter->CurrentWeapon));

	if (!ShooterCharacter->HasAuthority())
	{
		// 발사 판정은 서버가 하지만, 손맛을 위해 소유 클라이언트는 몽타주만 즉시 재생
		ShooterCharacter->PlayLocalActionMontage(ShooterCharacter->FireMontage);
		ShooterCharacter->ServerStartAttack();
		return;
	}

	if (!CanFireInCurrentState())
	{
		return;
	}

	RefreshWeaponMode();
	ShooterCharacter->RefreshMovementState();

	if (ShooterCharacter->MovementState == EMovementState::Run)
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}

	if (ShooterCharacter->CombatState == ECombatState::Reload)
	{
		CancelReloadInternal();
	}

	if (!ShooterCharacter->CurrentWeapon || !ShooterCharacter->CurrentWeapon->CanAttack())
	{
		RefreshCombatState();
		return;
	}

	ShooterCharacter->bFireHeld = true;

	switch (ShooterCharacter->WeaponMode)
	{
	case EWeaponMode::Primary:
	case EWeaponMode::Secondary:
		ShooterCharacter->CombatState = ECombatState::Fire;
		if (ShooterCharacter->WeaponMode == EWeaponMode::Secondary)
		{
			if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
			{
				BeginSecondaryCooldownInternal(RangedWeapon->GetReuseCooldown());
			}
		}
		break;
	case EWeaponMode::Melee:
		ShooterCharacter->bIsMeleeAttacking = true;
		ShooterCharacter->CombatState = ECombatState::Attack;
		break;
	case EWeaponMode::None:
	default:
		ShooterCharacter->CombatState = ECombatState::Idle;
		return;
	}

	// 전투 상태가 확정된 뒤에만 서버에서 실제 발사와 몽타주를 진행
	ShooterCharacter->PlayAnimMontage(ShooterCharacter->FireMontage);
	ShooterCharacter->CurrentWeapon->StartAttack();
}

void UShooterCombatComponent::TryStopAttack()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStopAttack CurrentWeapon=%s"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), *GetNameSafe(ShooterCharacter->CurrentWeapon));
	ShooterCharacter->bFireHeld = false;
	ShooterCharacter->bIsMeleeAttacking = false;

	if (ShooterCharacter->HasAuthority())
	{
		if (ShooterCharacter->CurrentWeapon)
		{
			ShooterCharacter->CurrentWeapon->StopAttack();
		}
	}
	else
	{
		ShooterCharacter->ServerStopAttack();
	}

	RefreshCombatState();
}

void UShooterCombatComponent::RefreshCombatState()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 무기 타입에 따라 가능한 전투 상태 집합이 바뀌므로 항상 먼저 무기 모드를 동기화
	RefreshWeaponMode();

	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		ShooterCharacter->bIsReloading = RangedWeapon->IsReloading();
		ShooterCharacter->bSecondaryOnCooldown = (ShooterCharacter->WeaponMode == EWeaponMode::Secondary) && RangedWeapon->IsOnReuseCooldown();
	}
	else
	{
		ShooterCharacter->bIsReloading = false;
		ShooterCharacter->bSecondaryOnCooldown = false;
	}

	switch (ShooterCharacter->WeaponMode)
	{
	case EWeaponMode::Primary:
		if (ShooterCharacter->bIsReloading)
		{
			ShooterCharacter->CombatState = ECombatState::Reload;
		}
		else if (ShooterCharacter->bAimHeld)
		{
			ShooterCharacter->CombatState = ECombatState::Aim;
		}
		else if (ShooterCharacter->bFireHeld)
		{
			ShooterCharacter->CombatState = ECombatState::Fire;
		}
		else
		{
			ShooterCharacter->CombatState = ECombatState::Idle;
		}
		break;
	case EWeaponMode::Secondary:
		if (ShooterCharacter->bIsReloading)
		{
			ShooterCharacter->CombatState = ECombatState::Reload;
		}
		else if (ShooterCharacter->bSecondaryOnCooldown)
		{
			ShooterCharacter->CombatState = ECombatState::Cooldown;
		}
		else if (ShooterCharacter->bFireHeld)
		{
			ShooterCharacter->CombatState = ECombatState::Fire;
		}
		else if (ShooterCharacter->bAimHeld)
		{
			ShooterCharacter->CombatState = ECombatState::Aim;
		}
		else
		{
			ShooterCharacter->CombatState = ECombatState::Idle;
		}
		break;
	case EWeaponMode::Melee:
		ShooterCharacter->CombatState = ShooterCharacter->bIsMeleeAttacking ? ECombatState::Attack : ECombatState::Idle;
		break;
	case EWeaponMode::None:
	default:
		ShooterCharacter->CombatState = ECombatState::Idle;
		break;
	}
}

void UShooterCombatComponent::RefreshWeaponMode()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	switch (ShooterCharacter->GetWeaponType())
	{
	case EWeaponType::Rifle:
		ShooterCharacter->WeaponMode = EWeaponMode::Primary;
		break;
	case EWeaponType::Pistol:
		ShooterCharacter->WeaponMode = EWeaponMode::Secondary;
		break;
	case EWeaponType::Melee:
		ShooterCharacter->WeaponMode = EWeaponMode::Melee;
		break;
	case EWeaponType::Unarmed:
	default:
		ShooterCharacter->WeaponMode = EWeaponMode::None;
		break;
	}
}

void UShooterCombatComponent::ResolveStateConflicts()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (ShooterCharacter->MovementState == EMovementState::Run
		&& (ShooterCharacter->bAimHeld || ShooterCharacter->bFireHeld || ShooterCharacter->bIsReloading))
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}

	if (ShooterCharacter->CombatState == ECombatState::Reload && ShooterCharacter->bFireHeld)
	{
		CancelReloadInternal();
	}
}

void UShooterCombatComponent::StopAimInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->bAimHeld = false;
	ShooterCharacter->bIsAiming = false;

	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->SetAiming(false);
	}
}

void UShooterCombatComponent::BeginReloadInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 리로드는 무기 내부 상태와 별개로 캐릭터 전투 상태도 함께 잠궈야 함
	ShooterCharacter->bIsReloading = true;
	ShooterCharacter->CombatState = ECombatState::Reload;
	ShooterCharacter->PlayAnimMontage(ShooterCharacter->ReloadMontage);
}

void UShooterCombatComponent::CancelReloadInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->bIsReloading)
	{
		return;
	}

	ShooterCharacter->bIsReloading = false;
	ShooterCharacter->StopAnimMontage(ShooterCharacter->ReloadMontage);
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->CancelReload();
	}
	RefreshCombatState();
}

void UShooterCombatComponent::FinishReloadInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->bIsReloading = false;
	RefreshCombatState();
}

void UShooterCombatComponent::BeginSecondaryCooldownInternal(float CooldownDuration)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (CooldownDuration <= 0.0f)
	{
		ShooterCharacter->bSecondaryOnCooldown = false;
		return;
	}

	ShooterCharacter->bSecondaryOnCooldown = true;
	ShooterCharacter->GetWorldTimerManager().ClearTimer(ShooterCharacter->SecondaryCooldownStateTimerHandle);
	ShooterCharacter->GetWorldTimerManager().SetTimer(
		ShooterCharacter->SecondaryCooldownStateTimerHandle,
		ShooterCharacter,
		&AShooterCharacter::FinishSecondaryCooldownInternal,
		CooldownDuration,
		false);
}

void UShooterCombatComponent::FinishSecondaryCooldownInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->bSecondaryOnCooldown = false;
	RefreshCombatState();
}

void UShooterCombatComponent::HandleReloadCommitNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->FinishReload();
	}

	FinishReloadInternal();
}

bool UShooterCombatComponent::CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const
{
	switch (InWeaponMode)
	{
	case EWeaponMode::Primary:
		return NextState == ECombatState::Idle || NextState == ECombatState::Fire || NextState == ECombatState::Aim || NextState == ECombatState::Reload;
	case EWeaponMode::Secondary:
		return NextState == ECombatState::Idle || NextState == ECombatState::Fire || NextState == ECombatState::Aim || NextState == ECombatState::Reload || NextState == ECombatState::Cooldown;
	case EWeaponMode::Melee:
		return NextState == ECombatState::Idle || NextState == ECombatState::Attack;
	case EWeaponMode::None:
	default:
		return NextState == ECombatState::Idle;
	}
}

bool UShooterCombatComponent::CanAimInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter
		&& !ShooterCharacter->bIsDead
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanReloadInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter && !ShooterCharacter->bIsDead && !ShooterCharacter->bIsSliding
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanFireInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->bIsDead || ShooterCharacter->CurrentWeapon == nullptr)
	{
		return false;
	}

	if (ShooterCharacter->bIsReloading)
	{
		return false;
	}

	if (ShooterCharacter->WeaponMode == EWeaponMode::Secondary && ShooterCharacter->bSecondaryOnCooldown)
	{
		return false;
	}

	return true;
}
