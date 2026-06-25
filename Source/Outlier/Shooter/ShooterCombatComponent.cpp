// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterCombatComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Weapon/RangedWeaponBase.h"
#include "Shooter/ShooterMovementComponent.h"
#include "OutlierNetUtils.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
float GetMontageSectionDurationOrFullLength(const UAnimMontage* Montage, FName SectionName)
{
	if (!Montage)
	{
		return 0.0f;
	}

	if (SectionName == NAME_None)
	{
		return Montage->GetPlayLength();
	}

	const int32 SectionIndex = Montage->GetSectionIndex(SectionName);
	if (SectionIndex == INDEX_NONE)
	{
		return Montage->GetPlayLength();
	}

	float SectionStartTime = 0.0f;
	float SectionEndTime = 0.0f;
	Montage->GetSectionStartAndEndTime(SectionIndex, SectionStartTime, SectionEndTime);
	return FMath::Max(0.0f, SectionEndTime - SectionStartTime);
}

}

UShooterCombatComponent::UShooterCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShooterCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UShooterCombatComponent, bIsAiming);
	DOREPLIFETIME(UShooterCombatComponent, bIsReloading);
	DOREPLIFETIME(UShooterCombatComponent, bSecondaryOnCooldown);
	DOREPLIFETIME(UShooterCombatComponent, bIsMeleeAttacking);
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
		if (ShooterCharacter->CanStartAction(EShooterActionLock::Reload))
		{
			ShooterCharacter->StopLean();
			ShooterCharacter->PlayFirstPersonMontage(ShooterCharacter->FirstPersonReloadMontage);
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("%s %s TryReload local prediction skipped ActionLock=%d"),
				OutlierNet::GetNetPrefix(ShooterCharacter),
				*ShooterCharacter->GetName(),
				static_cast<int32>(ShooterCharacter->GetActionLock())
			);
		}
		ShooterCharacter->ServerReload();
		return;
	}

	if (!ShooterCharacter->CanStartAction(EShooterActionLock::Reload))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s TryReload server blocked ActionLock=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*ShooterCharacter->GetName(),
			static_cast<int32>(ShooterCharacter->GetActionLock())
		);
		return;
	}

	if (ShooterCharacter->CurrentWeapon && ShooterCharacter->CurrentWeapon->IsAttacking())
	{
		UE_LOG(LogTemp, Log, TEXT("%s %s TryReload stopping active attack before reload"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName());
		ShooterCharacter->CurrentWeapon->StopAttack();
	}

	RefreshWeaponMode();
	RefreshCombatState();
	if (!CanReloadInCurrentState())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s TryReload blocked State Combat=%d Move=%d WeaponMode=%d Reloading=%d Sliding=%d WantsToFire=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*ShooterCharacter->GetName(),
			static_cast<int32>(ShooterCharacter->CombatState),
			static_cast<int32>(ShooterCharacter->MovementState),
			static_cast<int32>(ShooterCharacter->WeaponMode),
			bIsReloading ? 1 : 0,
			ShooterCharacter->IsSliding() ? 1 : 0,
			bWantsToFire ? 1 : 0
		);
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
	if (!RangedWeapon || !RangedWeapon->CanReload())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s TryReload weapon rejected reload Weapon=%s WeaponReloading=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*ShooterCharacter->GetName(),
			*GetNameSafe(ShooterCharacter->CurrentWeapon),
			RangedWeapon ? (RangedWeapon->IsReloading() ? 1 : 0) : -1
		);
		return;
	}

	if (ShooterCharacter->IsSprinting())
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}
	ShooterCharacter->StopLean();

	if (ShooterCharacter->CombatState == ECombatState::Aim)
	{
		StopAimInternal();
	}

	RangedWeapon->BeginReload();
	if (!RangedWeapon->IsReloading())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryReload BeginReload did not latch weapon reload state"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName());
		return;
	}

	BeginReloadInternal();
}

void UShooterCombatComponent::HandleAimPressed()
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

	if (ShooterCharacter->IsSprinting())
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}

	// CombatComponent가 조준 입력 의도와 확정된 조준 상태를 함께 관리함
	bWantsToAim = true;
	bIsAiming = true;
	ShooterCharacter->CombatState = ECombatState::Aim;
	RangedWeapon->SetAiming(true);
}

void UShooterCombatComponent::HandleAimReleased()
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

bool UShooterCombatComponent::ShouldDelayFireForSprintExit(const AShooterCharacter& ShooterCharacter) const
{
	if (!ShooterCharacter.IsLocallyControlled())
	{
		return false;
	}

	const USkeletalMeshComponent* FirstPersonMesh = ShooterCharacter.GetFirstPersonMesh();
	const UShooterFirstPersonAnimInstance* FirstPersonAnimInstance = FirstPersonMesh
		? Cast<UShooterFirstPersonAnimInstance>(FirstPersonMesh->GetAnimInstance())
		: nullptr;

	return FirstPersonAnimInstance
		&& FirstPersonAnimInstance->GetViewModelSprintAlpha() > SprintFireAllowedAlpha;
}

void UShooterCombatComponent::QueueSprintExitFire(AShooterCharacter& ShooterCharacter)
{
	bPendingSprintExitFire = true;
	bWantsToFire = true;

	if (ShooterCharacter.IsSprinting())
	{
		if (!ShooterCharacter.HasAuthority())
		{
			ShooterCharacter.ServerSetSprintState(false);
		}

		ShooterCharacter.StopSprintInternal();
		ShooterCharacter.RefreshMovementState();
	}

	ShooterCharacter.GetWorldTimerManager().ClearTimer(PendingSprintExitFireTimerHandle);
	ShooterCharacter.GetWorldTimerManager().SetTimer(
		PendingSprintExitFireTimerHandle,
		this,
		&UShooterCombatComponent::RetryPendingSprintExitFire,
		FMath::Max(SprintExitFireRetryInterval, KINDA_SMALL_NUMBER),
		false
	);
}

void UShooterCombatComponent::RetryPendingSprintExitFire()
{
	if (!bPendingSprintExitFire || !bWantsToFire)
	{
		ClearPendingSprintExitFire();
		return;
	}

	TryStartAttack();
}

void UShooterCombatComponent::ClearPendingSprintExitFire()
{
	bPendingSprintExitFire = false;

	if (AShooterCharacter* ShooterCharacter = GetShooterCharacter())
	{
		ShooterCharacter->GetWorldTimerManager().ClearTimer(PendingSprintExitFireTimerHandle);
	}
}

void UShooterCombatComponent::TryStartAttack()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStartAttack CurrentWeapon=%s"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), *GetNameSafe(ShooterCharacter->CurrentWeapon));

	if (ShouldDelayFireForSprintExit(*ShooterCharacter))
	{
		QueueSprintExitFire(*ShooterCharacter);
		return;
	}

	ClearPendingSprintExitFire();

	if (!ShooterCharacter->HasAuthority())
	{
		// 발사 판정은 서버가 하지만, 손맛을 위해 소유 클라이언트는 몽타주만 즉시 재생
		ShooterCharacter->ServerStartAttack();
		return;
	}

	if (!CanFireInCurrentState())
	{
		return;
	}

	RefreshWeaponMode();
	ShooterCharacter->RefreshMovementState();

	if (ShooterCharacter->IsSprinting())
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

	bWantsToFire = true;

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
		bIsMeleeAttacking = true;
		ShooterCharacter->CombatState = ECombatState::Attack;
		break;
	case EWeaponMode::None:
	default:
		ShooterCharacter->CombatState = ECombatState::Idle;
		return;
	}

	// 전투 상태가 확정된 뒤에만 서버에서 실제 발사와 몽타주를 진행
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
	ClearPendingSprintExitFire();
	bWantsToFire = false;
	bIsMeleeAttacking = false;

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

void UShooterCombatComponent::HandleWeaponAttackStopped()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	bWantsToFire = false;
	bIsMeleeAttacking = false;
	RefreshCombatState();
}

void UShooterCombatComponent::HandleAutoReloadRequested()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
	if (!RangedWeapon || !RangedWeapon->CanReload() || bIsReloading)
	{
		return;
	}

	if (ShooterCharacter->CurrentWeapon->IsAttacking())
	{
		ShooterCharacter->CurrentWeapon->StopAttack();
	}

	if (!ShooterCharacter->CanStartAction(EShooterActionLock::Reload))
	{
		return;
	}

	if (ShooterCharacter->IsSprinting())
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}
	ShooterCharacter->StopLean();

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

void UShooterCombatComponent::RefreshCombatState()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 무기 타입에 따라 가능한 전투 상태 집합이 바뀌므로 항상 먼저 무기 모드를 동기화
	// CombatComponent가 전투 쪽 임시 플래그와 공개 combat enum을 단일 책임으로 갱신함
	RefreshWeaponMode();
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		bIsReloading = RangedWeapon->IsReloading();
		bSecondaryOnCooldown = (ShooterCharacter->WeaponMode == EWeaponMode::Secondary) && RangedWeapon->IsOnReuseCooldown();
	}
	else
	{
		bIsReloading = false;
		bSecondaryOnCooldown = false;
	}

	if (ShooterCharacter->WeaponMode != EWeaponMode::Melee
		&& ShooterCharacter->CurrentWeapon
		&& !ShooterCharacter->CurrentWeapon->IsAttacking()
		&& bWantsToFire)
	{
		UE_LOG(LogTemp, Log, TEXT("%s %s RefreshCombatState clearing stale fire intent"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName());
		bWantsToFire = false;
	}

	switch (ShooterCharacter->WeaponMode)
	{
	case EWeaponMode::Primary:
		if (bIsReloading)
		{
			ShooterCharacter->CombatState = ECombatState::Reload;
		}
		else if (bWantsToAim)
		{
			ShooterCharacter->CombatState = ECombatState::Aim;
		}
		else if (bWantsToFire)
		{
			ShooterCharacter->CombatState = ECombatState::Fire;
		}
		else
		{
			ShooterCharacter->CombatState = ECombatState::Idle;
		}
		break;
	case EWeaponMode::Secondary:
		if (bIsReloading)
		{
			ShooterCharacter->CombatState = ECombatState::Reload;
		}
		else if (bSecondaryOnCooldown)
		{
			ShooterCharacter->CombatState = ECombatState::Cooldown;
		}
		else if (bWantsToFire)
		{
			ShooterCharacter->CombatState = ECombatState::Fire;
		}
		else if (bWantsToAim)
		{
			ShooterCharacter->CombatState = ECombatState::Aim;
		}
		else
		{
			ShooterCharacter->CombatState = ECombatState::Idle;
		}
		break;
	case EWeaponMode::Melee:
		ShooterCharacter->CombatState = bIsMeleeAttacking ? ECombatState::Attack : ECombatState::Idle;
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
		&& (bWantsToAim || bWantsToFire || bIsReloading))
	{
		ShooterCharacter->StopSprintInternal();
		ShooterCharacter->RefreshMovementState();
	}

	if (ShooterCharacter->IsSprinting() || bIsReloading)
	{
		ShooterCharacter->StopLean();
	}

	if (ShooterCharacter->CombatState == ECombatState::Reload && bWantsToFire)
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

	bWantsToAim = false;
	bIsAiming = false;

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
	// 리로드 상태는 여기서 먼저 잠그고, 이후 RefreshCombatState에서 무기 상태와 다시 맞춤
	bIsReloading = true;
	ShooterCharacter->StopLean();
	ShooterCharacter->BeginActionLock(EShooterActionLock::Reload);
	ShooterCharacter->CombatState = ECombatState::Reload;
	ShooterCharacter->ForceNetUpdate();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s BeginReloadInternal WeaponType=%d FPReload=%s TPReload=%s Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter),
		*ShooterCharacter->GetName(),
		static_cast<int32>(ShooterCharacter->GetWeaponType()),
		*GetNameSafe(ShooterCharacter->FirstPersonReloadMontage),
		*GetNameSafe(ShooterCharacter->ThirdPersonReloadMontage),
		ShooterCharacter->IsLocallyControlled() ? 1 : 0,
		ShooterCharacter->HasAuthority() ? 1 : 0);

	ShooterCharacter->MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Reload, ShooterCharacter->GetWeaponType());

	if (ShooterCharacter->IsLocallyControlled())
	{
		ShooterCharacter->PlayFirstPersonActionMontage(EShooterMontageAction::Reload, ShooterCharacter->GetWeaponType());
	}
	else if (ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ClientPlayFirstPersonActionMontage(EShooterMontageAction::Reload, ShooterCharacter->GetWeaponType());
	}
}

void UShooterCombatComponent::CancelReloadInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !bIsReloading)
	{
		return;
	}

	bIsReloading = false;
	ShooterCharacter->EndActionLock(EShooterActionLock::Reload);

	ShooterCharacter->StopSplitMontages(
		ShooterCharacter->FirstPersonReloadMontage,
		ShooterCharacter->ThirdPersonReloadMontage);
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

	if (!ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (!bIsReloading)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Reload End"));
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		if (RangedWeapon->IsReloading())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s %s Reload end applying missed commit"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName());
			RangedWeapon->FinishReload();
		}
	}

	bIsReloading = false;
	ShooterCharacter->EndActionLock(EShooterActionLock::Reload);
	RefreshCombatState();
	ShooterCharacter->ForceNetUpdate();
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
		bSecondaryOnCooldown = false;
		return;
	}

	bSecondaryOnCooldown = true;
	ShooterCharacter->GetWorldTimerManager().ClearTimer(SecondaryCooldownStateTimerHandle);
	ShooterCharacter->GetWorldTimerManager().SetTimer(
		SecondaryCooldownStateTimerHandle,
		this,
		&UShooterCombatComponent::FinishSecondaryCooldownInternal,
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

	bSecondaryOnCooldown = false;
	RefreshCombatState();
}

void UShooterCombatComponent::ResetSecondaryCooldown()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->GetWorldTimerManager().ClearTimer(SecondaryCooldownStateTimerHandle);
	bSecondaryOnCooldown = false;
}

void UShooterCombatComponent::HandleReloadCommitNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s HandleReloadCommitNotify"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName());

	if (!ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->FinishReload();
	}
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
		&& !ShooterCharacter->IsActionLocked()
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanReloadInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
		return ShooterCharacter && !ShooterCharacter->bIsDead && !ShooterCharacter->IsSliding() && !ShooterCharacter->IsActionLocked()
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanFireInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->bIsDead || ShooterCharacter->CurrentWeapon == nullptr)
	{
		return false;
	}

	if (ShooterCharacter->IsActionLocked())
	{
		return false;
	}

	if (bIsReloading)
	{
		return false;
	}

	if (ShooterCharacter->WeaponMode == EWeaponMode::Secondary && bSecondaryOnCooldown)
	{
		return false;
	}

	return true;
}

void UShooterCombatComponent::ClearInputIntent()
{
	ClearPendingSprintExitFire();
	bWantsToAim = false;
	bWantsToFire = false;
}
