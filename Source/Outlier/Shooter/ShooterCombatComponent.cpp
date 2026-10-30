// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterCombatComponent.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "OutlierNetUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Shooter/ShooterPlayerController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Net/UnrealNetwork.h"

namespace
{
	//매 프레임마다 Socket 위치 넘겨주기.
	static const FName ADSFocusSocketName(TEXT("OpticAimPoint"));
	float ResolveADSFocusDistance(AWeaponBase* Weapon, const FVector& CameraLocation)
	{
		ARangedWeaponBase* Base = Cast<ARangedWeaponBase>(Weapon);

		if (Base)
		{
			if (!Base->GetFirstSightMesh()->DoesSocketExist(ADSFocusSocketName))
			{
				UE_LOG(LogTemp, Error, TEXT("NO Sockett"));

				return -1.0f;
			}
		}
		
		const FVector SocketLocation = Base->GetFirstSightMesh()->GetSocketLocation(ADSFocusSocketName);
		return FVector::Dist(CameraLocation, SocketLocation);
	}
}

UShooterCombatComponent::UShooterCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShooterCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	BindWeaponReuseCooldownObserver();
}

void UShooterCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindReloadMontageEndedDelegates();
	UnbindWeaponReuseCooldownObserver();
	Super::EndPlay(EndPlayReason);
}

void UShooterCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UShooterCombatComponent, bIsAiming);
	DOREPLIFETIME(UShooterCombatComponent, bIsReloading);
	DOREPLIFETIME(UShooterCombatComponent, bIsMeleeAttacking);
}

void UShooterCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsAiming()) return;

	//AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	//// Controller는 소유 클라이언트에만 리플리케이트됨 — 이 틱이 다른 플레이어의 화면(원격/시뮬레이트 프록시)에서
	//// 돌아가는 중이면 GetController()가 항상 null이라 아래에서 널 참조 크래시가 났었음
	//if (!ShooterCharacter || !ShooterCharacter->IsLocallyControlled())
	//{
	//	return;
	//}

	//ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
	//if (!RangedWeapon)
	//{
	//	return;
	//}

	////Socket,
	//AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(ShooterCharacter->GetController());
	//if (!ShooterController)
	//{
	//	return;
	//}
	//Socket,

	//if (const ULocalPlayer* LocalPlayer = ShooterController->GetLocalPlayer())
	//{
	//	if (const APlayerController* PC = LocalPlayer->GetPlayerController(GetWorld()))
	//	{
	//		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
	//		{
	//			const FVector CameraLocation = CameraManager->GetCameraLocation();

	//			ShooterController->SocketDistanceUpdate(ResolveADSFocusDistance(RangedWeapon, CameraLocation));
	//		}
	//	}
	//}
}

void UShooterCombatComponent::TryReload()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryReload CurrentWeapon=%s"), OutlierNet::GetNetPrefix(ShooterCharacter), *ShooterCharacter->GetName(), *GetNameSafe(ShooterCharacter->CurrentWeapon));

	if (!ShooterCharacter->HasAuthority())
	{
		// 1인칭 반응성은 로컬에서 먼저 주고, 실제 상태 전이는 서버가 확정
		if (ShooterCharacter->CanStartAction(EShooterActionLock::Reload))
		{
			ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon);
			if (RangedWeapon && RangedWeapon->CanReload())
			{
				ShooterCharacter->StopLean();
				SuspendAimInternal();

				bIsReloading = true;
				ShooterCharacter->BeginActionLock(EShooterActionLock::Reload);
				ShooterCharacter->RefreshMovementState();
				ShooterCharacter->CombatState = ECombatState::Reload;
				ShooterCharacter->PlayFirstPersonMontage(ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, true));
			}
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
	bWantsToFire = false;

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

	ShooterCharacter->StopLean();

	SuspendAimInternal();

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
	if (!ShooterCharacter || ShooterCharacter->IsDead() || ShooterCharacter->IsSuitTransitionBlocked())
	{
		return;
	}

	// 물리 입력에서만 순번을 발급한다. RPC와 이동 패킷은 같은 최종 의도를 전달한다.
	auto* Movement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement());
	const uint32 InputSequence = Movement->RecordActionInput(true);
	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetAimState(true, Movement->GetSprintInputIntent(),
			InputSequence, Movement->GetMovementInputEpoch(), Movement->GetAimInputSwitchId());
	}

	ApplyAimInput(true);
}

void UShooterCombatComponent::StartAimInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || bIsAiming)
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
	bIsAiming = true;
	ShooterCharacter->CombatState = ECombatState::Aim;
	RangedWeapon->SetAiming(true);

	NotifyAimPresentation();
}

void UShooterCombatComponent::OnRep_IsAiming()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	// 서버의 ADS가 먼저 도착해도 소유자의 Raise나 최신 Release를 건너뛰지 않는다.
	// 홀드 의도는 남겨 두고 교체/잠금 완료 경계에서 기존 복구 경로로 다시 평가한다.
	if (ShooterCharacter && ShooterCharacter->IsLocallyControlled() && bIsAiming
		&& (!bWantsToAim || !CanAimInCurrentState()))
	{
		SuspendAimInternal();
		RefreshCombatState();
		return;
	}
	if (ShooterCharacter && ShooterCharacter->IsLocallyControlled() && !bIsAiming
		&& bWantsToAim && CanAimInCurrentState())
	{
		// 이전 중지 복제가 Raise 완료보다 늦게 와도 유효한 현재 홀드를 지우지 않는다.
		// 잠금/지상/현재 무기 검사는 그대로 거쳐 소유자의 예측만 복구한다.
		RestoreAimIfRequested();
		RefreshCombatState();
		return;
	}

	// 복제로 바뀐 조준 상태도 로컬 상태 전이와 같은 표시 갱신을 적용한다.
	NotifyAimPresentation();
}

void UShooterCombatComponent::NotifyAimPresentation()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->OnShooterDynamicCrosshairChanged.Broadcast(bIsAiming);
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->SetSightAimMaterialFlag(bIsAiming);
		ShooterCharacter->OnShooterAimingBlur.Broadcast(bIsAiming, RangedWeapon->ResolveADSBlurStencil());
	}
}

void UShooterCombatComponent::HandleAimReleased()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	auto* Movement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement());
	const uint32 InputSequence = Movement->RecordActionInput(false);
	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetAimState(false, Movement->GetSprintInputIntent(),
			InputSequence, Movement->GetMovementInputEpoch(), Movement->GetAimInputSwitchId());
	}

	ApplyAimInput(false);
}

void UShooterCombatComponent::ApplyAimInput(bool bNewWantsToAim)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!bNewWantsToAim || ShooterCharacter->IsDead() || ShooterCharacter->IsSuitTransitionBlocked())
	{
		// 상태 적용은 과거 이동 재생에서도 호출된다. 물리 입력 취소 순번은 발급하지 않는다.
		bWantsToAim = false;
		SuspendAimInternal();
		RefreshCombatState();
		return;
	}

	bWantsToAim = true;
	// 공중/교체 중에도 Aim 의도는 Sprint를 취소한다. 실제 ADS의 허용 여부와 분리한다.
	if (ShooterCharacter->MovementComponent)
	{
		ShooterCharacter->MovementComponent->SetSprintIntent(false);
	}
	ShooterCharacter->RefreshMovementState();
	RestoreAimIfRequested();
	RefreshCombatState();
}

bool UShooterCombatComponent::ShouldDelayFireForSprintExit(const AShooterCharacter& ShooterCharacter) const
{
	if (!ShooterCharacter.IsLocallyControlled() || ShooterCharacter.GetWeaponType() == EWeaponType::Melee)
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
		ShooterCharacter.StopSprintInternal();
		if (!ShooterCharacter.HasAuthority())
		{
			const auto* Movement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter.GetCharacterMovement());
			// 발사 대기로 취소한 순번을 함께 보낸다. 늦은 취소 RPC가 이후 Toggle ON을 덮지 않는다.
			ShooterCharacter.ServerSetSprintState(false, Movement->GetActionInputRequest(), Movement->GetMovementInputEpoch());
		}

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

bool UShooterCombatComponent::IsActionLockBlockingAimFire(const AShooterCharacter& ShooterCharacter) const
{
	const EShooterActionLock ActionLock = ShooterCharacter.GetActionLock();
	return ShooterCharacter.IsSuitTransitionBlocked()
		|| (ActionLock != EShooterActionLock::None && ActionLock != EShooterActionLock::Slide);
}

void UShooterCombatComponent::TryStartAttack()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (ShooterCharacter && Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Input] Shooter=%s Authority=%d Weapon=%s SuitBlocked=%d Dead=%d ActionLock=%d Reloading=%d CanFire=%d"),
			*GetNameSafe(ShooterCharacter), ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter->CurrentWeapon),
			ShooterCharacter->IsSuitTransitionBlocked(), ShooterCharacter->IsDead(), static_cast<int32>(ShooterCharacter->GetActionLock()),
			bIsReloading, CanFireInCurrentState());
	}
	if (!ShooterCharacter || ShooterCharacter->IsSuitTransitionBlocked())
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

	if (ShooterCharacter->IsSprinting() && ShooterCharacter->WeaponMode != EWeaponMode::Melee)
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
		if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
		{
			// 재입력으로 공격 시간을 초기화하지 않고 다음 반복에 사용할 유지 의도만 갱신한다.
			MeleeWeapon->StartAttack();
		}
		RefreshCombatState();
		return;
	}

	// 근접 입력 유지는 무기가 관리하므로 총기의 달리기 해제용 발사 의도와 분리한다.
	bWantsToFire = ShooterCharacter->WeaponMode != EWeaponMode::Melee;

	switch (ShooterCharacter->WeaponMode)
	{
	case EWeaponMode::Primary:
	case EWeaponMode::Secondary:
		ShooterCharacter->CombatState = ECombatState::Fire;
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
	// 입력 해제는 서버에 전달하되 현재 휘두르기는 완료하고 다음 반복만 막는다.
	if (ShooterCharacter->GetWeaponType() == EWeaponType::Melee)
	{
		if (!ShooterCharacter->HasAuthority())
		{
			ShooterCharacter->ServerStopAttack();
		}
		else if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
		{
			MeleeWeapon->ReleaseAttack();
		}
		return;
	}

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

void UShooterCombatComponent::CancelMeleeAttack()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->StopAttack();
	}
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
	bWantsToFire = false;

	if (!ShooterCharacter->CanStartAction(EShooterActionLock::Reload))
	{
		return;
	}

	ShooterCharacter->StopLean();

	SuspendAimInternal();

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
		bIsReloading = bIsReloading || RangedWeapon->IsReloading();
	}
	else
	{
		bIsReloading = false;
	}
	const bool bSecondaryOnCooldown = IsSecondaryOnCooldown();

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
		else if (bIsAiming)
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
		else if (bIsAiming)
		{
			ShooterCharacter->CombatState = ECombatState::Aim;
		}
		else
		{
			ShooterCharacter->CombatState = ECombatState::Idle;
		}
		break;
	case EWeaponMode::Melee:
	{
		const AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon);
		const EMeleeAttackPhase Phase = MeleeWeapon ? MeleeWeapon->GetAttackPhase() : EMeleeAttackPhase::Idle;
		bIsMeleeAttacking = Phase != EMeleeAttackPhase::Idle;
		ShooterCharacter->CombatState = Phase == EMeleeAttackPhase::Recovery
			? ECombatState::Recovery
			: (bIsMeleeAttacking ? ECombatState::Attack : ECombatState::Idle);
		break;
	}
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
		&& (bWantsToAim || bWantsToFire))
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
	if (AShooterCharacter* ShooterCharacter = GetShooterCharacter())
	{
		CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
			->CancelActionInput(false, true);
	}
	bWantsToAim = false;
	SuspendAimInternal();
}

void UShooterCombatComponent::SuspendAimInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	const bool bWasAiming = bIsAiming;
	bIsAiming = false;

	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->SetAiming(false);
		RangedWeapon->SetSightAimMaterialFlag(false);
	}

	if (bWasAiming)
	{
		NotifyAimPresentation();
	}
}

void UShooterCombatComponent::RestoreAimIfRequested()
{
	if (!bWantsToAim || bIsAiming)
	{
		return;
	}

	RefreshWeaponMode();
	if (CanAimInCurrentState())
	{
		StartAimInternal();
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
	SuspendAimInternal();
	bIsReloading = true;
	ShooterCharacter->StopLean();
	ShooterCharacter->BeginActionLock(EShooterActionLock::Reload);
	ShooterCharacter->RefreshMovementState();
	ShooterCharacter->CombatState = ECombatState::Reload;
	ShooterCharacter->ForceNetUpdate();
	BindReloadMontageEndedDelegates();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s BeginReloadInternal WeaponType=%d FPReload=%s TPReload=%s Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter),
		*ShooterCharacter->GetName(),
		static_cast<int32>(ShooterCharacter->GetWeaponType()),
		*GetNameSafe(ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, true)),
		*GetNameSafe(ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, false)),
		ShooterCharacter->IsLocallyControlled() ? 1 : 0,
		ShooterCharacter->HasAuthority() ? 1 : 0);

	ShooterCharacter->MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Reload, ShooterCharacter->GetWeaponType());
	if (ShooterCharacter->HasAuthority())
	{
		UAnimInstance* ReloadInstance = BoundThirdPersonReloadInstance.Get();
		UAnimMontage* ReloadMontage = ActiveThirdPersonReloadMontage.Get();
		if (!ReloadInstance || !ReloadMontage || !ReloadInstance->Montage_IsPlaying(ReloadMontage))
		{
			// 호환 몽타주가 없거나 재생에 실패하면 Notify를 기다리며 잠긴 상태로 남기지 않는다.
			UE_LOG(LogTemp, Warning, TEXT("[PresentationMontage] Reload cancelled: no playable authoritative montage Owner=%s"),
				*ShooterCharacter->GetName());
			CancelReloadInternal();
			return;
		}
	}

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
	if (!ShooterCharacter || (!bIsReloading && ShooterCharacter->GetActionLock() != EShooterActionLock::Reload))
	{
		return;
	}

	// 애니메이션 소유 반복음만 정리한다. 이동/LowHP 등 실제 상태가 소유한 소리는 유지한다.
	if (ShooterCharacter->MovementAudioSubsystem) ShooterCharacter->MovementAudioSubsystem->StopAnimationAudioLoops(ShooterCharacter);
	bIsReloading = false;
	ShooterCharacter->EndActionLock(EShooterActionLock::Reload);
	UAnimMontage* FirstPersonMontage = ActiveFirstPersonReloadMontage.IsValid()
		? ActiveFirstPersonReloadMontage.Get() : ShooterCharacter->GetFirstPersonReloadMontage();
	UAnimMontage* ThirdPersonMontage = ActiveThirdPersonReloadMontage.IsValid()
		? ActiveThirdPersonReloadMontage.Get() : ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, false);
	UnbindReloadMontageEndedDelegates();

	ShooterCharacter->StopSplitMontages(FirstPersonMontage, ThirdPersonMontage);
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->CancelReload();
	}
	RefreshCombatState();
	RestoreAimIfRequested();
	ShooterCharacter->RefreshMovementState();
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

	if (!bIsReloading && ShooterCharacter->GetActionLock() != EShooterActionLock::Reload)
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

	if (ShooterCharacter->MovementAudioSubsystem) ShooterCharacter->MovementAudioSubsystem->StopAnimationAudioLoops(ShooterCharacter);
	bIsReloading = false;
	ShooterCharacter->EndActionLock(EShooterActionLock::Reload);
	UnbindReloadMontageEndedDelegates();
	RefreshCombatState();
	RestoreAimIfRequested();
	ShooterCharacter->RefreshMovementState();
	ShooterCharacter->ForceNetUpdate();
}

void UShooterCombatComponent::OnRep_IsReloading()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (bIsReloading)
	{
		SuspendAimInternal();
		ShooterCharacter->RefreshMovementState();
		return;
	}

	if (ShooterCharacter->MovementAudioSubsystem) ShooterCharacter->MovementAudioSubsystem->StopAnimationAudioLoops(ShooterCharacter);
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		RangedWeapon->CancelReload();
	}
	ShooterCharacter->EndActionLock(EShooterActionLock::Reload);
	UnbindReloadMontageEndedDelegates();
	RefreshCombatState();
	RestoreAimIfRequested();
	ShooterCharacter->RefreshMovementState();
}

void UShooterCombatComponent::BindReloadMontageEndedDelegates()
{
	UnbindReloadMontageEndedDelegates();
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ActiveFirstPersonReloadMontage = ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, true);
	ActiveThirdPersonReloadMontage = ShooterCharacter->GetActionMontage(EShooterMontageAction::Reload, false);
	BoundFirstPersonReloadInstance = ShooterCharacter->GetFirstPersonMesh()
		? ShooterCharacter->GetFirstPersonMesh()->GetAnimInstance() : nullptr;
	BoundThirdPersonReloadInstance = ShooterCharacter->GetMesh()
		? ShooterCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (UAnimInstance* ThirdPersonAnimInstance = BoundThirdPersonReloadInstance.Get())
	{
		ThirdPersonAnimInstance->OnMontageEnded.AddUniqueDynamic(this, &UShooterCombatComponent::HandleReloadMontageEnded);
	}

	if (UAnimInstance* FirstPersonAnimInstance = BoundFirstPersonReloadInstance.Get())
	{
		FirstPersonAnimInstance->OnMontageEnded.AddUniqueDynamic(this, &UShooterCombatComponent::HandleReloadMontageEnded);
	}
}

void UShooterCombatComponent::UnbindReloadMontageEndedDelegates()
{
	// 교체 후 Mesh에서 조회하면 새 인스턴스가 나온다. 실제 연결했던 이전 인스턴스에서 제거한다.
	if (UAnimInstance* ThirdPersonAnimInstance = BoundThirdPersonReloadInstance.Get())
	{
		ThirdPersonAnimInstance->OnMontageEnded.RemoveDynamic(this, &UShooterCombatComponent::HandleReloadMontageEnded);
	}

	if (UAnimInstance* FirstPersonAnimInstance = BoundFirstPersonReloadInstance.Get())
	{
		FirstPersonAnimInstance->OnMontageEnded.RemoveDynamic(this, &UShooterCombatComponent::HandleReloadMontageEnded);
	}
	BoundFirstPersonReloadInstance.Reset();
	BoundThirdPersonReloadInstance.Reset();
	ActiveFirstPersonReloadMontage.Reset();
	ActiveThirdPersonReloadMontage.Reset();
}

void UShooterCombatComponent::HandleReloadMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	const bool bIsReloadMontage =
		Montage && (Montage == ActiveFirstPersonReloadMontage.Get() ||
		Montage == ActiveThirdPersonReloadMontage.Get());
	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s HandleReloadMontageEnded Montage=%s Interrupted=%d IsReloadMontage=%d Reloading=%d ActionLock=%d"),
		OutlierNet::GetNetPrefix(ShooterCharacter),
		*ShooterCharacter->GetName(),
		*GetNameSafe(Montage),
		bInterrupted ? 1 : 0,
		bIsReloadMontage ? 1 : 0,
		bIsReloading ? 1 : 0,
		static_cast<int32>(ShooterCharacter->GetActionLock()));
	if (!bIsReloadMontage)
	{
		return;
	}

	if (bInterrupted)
	{
		CancelReloadInternal();
		return;
	}

	FinishReloadInternal();
}

void UShooterCombatComponent::HandleReloadCommitNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsSuitTransitionBlocked())
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

void UShooterCombatComponent::HandleMeleeHitNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Notify] Event=Hit Shooter=%s Authority=%d Weapon=%s"),
		*GetNameSafe(ShooterCharacter), ShooterCharacter && ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter ? ShooterCharacter->CurrentWeapon : nullptr));
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->HandleHitNotify();
	}
}

void UShooterCombatComponent::HandleMeleeTraceBeginNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Notify] Event=TraceBegin Shooter=%s Authority=%d Weapon=%s"),
		*GetNameSafe(ShooterCharacter), ShooterCharacter && ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter ? ShooterCharacter->CurrentWeapon : nullptr));
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->BeginMeleeTrace();
	}
}

void UShooterCombatComponent::HandleMeleeTraceTickNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Notify] Event=TraceTick Shooter=%s Authority=%d Weapon=%s"),
		*GetNameSafe(ShooterCharacter), ShooterCharacter && ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter ? ShooterCharacter->CurrentWeapon : nullptr));
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->TickMeleeTrace();
	}
}

void UShooterCombatComponent::HandleMeleeTraceEndNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Notify] Event=TraceEnd Shooter=%s Authority=%d Weapon=%s"),
		*GetNameSafe(ShooterCharacter), ShooterCharacter && ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter ? ShooterCharacter->CurrentWeapon : nullptr));
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->EndMeleeTrace();
	}
}

void UShooterCombatComponent::HandleMeleeRecoveryEndNotify()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UE_LOG(LogTemp, Log, TEXT("[MeleeDiag][Notify] Event=RecoveryEnd Shooter=%s Authority=%d Weapon=%s"),
		*GetNameSafe(ShooterCharacter), ShooterCharacter && ShooterCharacter->HasAuthority(), *GetNameSafe(ShooterCharacter ? ShooterCharacter->CurrentWeapon : nullptr));
	if (!ShooterCharacter || !ShooterCharacter->HasAuthority())
	{
		return;
	}

	if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
	{
		MeleeWeapon->HandleRecoveryEndNotify();
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
		return NextState == ECombatState::Idle || NextState == ECombatState::Attack || NextState == ECombatState::Recovery;
	case EWeaponMode::None:
	default:
		return NextState == ECombatState::Idle;
	}
}

bool UShooterCombatComponent::CanAimInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter
		&& !ShooterCharacter->IsDead()
		&& !ShooterCharacter->IsSuitTransitionBlocked()
		&& Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon)
		&& !IsActionLockBlockingAimFire(*ShooterCharacter)
		&& !ShooterCharacter->IsWeaponSwitchInProgress()
		&& ShooterCharacter->GetCharacterMovement()->IsMovingOnGround()
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanReloadInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDead() || ShooterCharacter->IsSuitTransitionBlocked())
	{
		return false;
	}

	const bool bActionLockedByNonSlide =
		ShooterCharacter->IsActionLocked() &&
		ShooterCharacter->GetActionLock() != EShooterActionLock::Slide;

	return !bActionLockedByNonSlide
		&& (ShooterCharacter->WeaponMode == EWeaponMode::Primary || ShooterCharacter->WeaponMode == EWeaponMode::Secondary);
}

bool UShooterCombatComponent::CanFireInCurrentState() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDead() || ShooterCharacter->IsSuitTransitionBlocked()
		|| ShooterCharacter->CurrentWeapon == nullptr)
	{
		return false;
	}

	if (IsActionLockBlockingAimFire(*ShooterCharacter))
	{
		return false;
	}

	// 총기는 현재 Hip/ADS 설정으로 발사한다. Slide 전용 시작 제한은 근접 옵션에만 적용한다.
	if (ShooterCharacter->IsSliding()
		&& ShooterCharacter->GetWeaponType() == EWeaponType::Melee
		&& !ShooterCharacter->AllowsMeleeAttackWhileSliding())
	{
		return false;
	}

	if (bIsReloading)
	{
		return false;
	}

	if (ShooterCharacter->WeaponMode == EWeaponMode::Secondary && IsSecondaryOnCooldown())
	{
		return false;
	}

	return true;
}

bool UShooterCombatComponent::IsSecondaryOnCooldown() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	const ARangedWeaponBase* RangedWeapon = ShooterCharacter
		? Cast<ARangedWeaponBase>(ShooterCharacter->CurrentWeapon)
		: nullptr;
	return ShooterCharacter
		&& ShooterCharacter->WeaponMode == EWeaponMode::Secondary
		&& RangedWeapon
		&& RangedWeapon->IsOnReuseCooldown();
}

void UShooterCombatComponent::BindWeaponReuseCooldownObserver()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter
		? ShooterCharacter->GetOutlierAbilitySystemComponent()
		: nullptr;
	if (!AbilitySystem || WeaponReuseCooldownTagChangedHandle.IsValid())
	{
		return;
	}

	WeaponReuseCooldownTagChangedHandle = AbilitySystem->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Weapon::Reuse(),
		EGameplayTagEventType::AnyCountChange).AddUObject(
			this,
			&UShooterCombatComponent::HandleWeaponReuseCooldownTagChanged);
}

void UShooterCombatComponent::UnbindWeaponReuseCooldownObserver()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter
		? ShooterCharacter->GetOutlierAbilitySystemComponent()
		: nullptr;
	if (AbilitySystem && WeaponReuseCooldownTagChangedHandle.IsValid())
	{
		AbilitySystem->RegisterGameplayTagEvent(
			OutlierGameplayTags::Cooldown::Weapon::Reuse(),
			EGameplayTagEventType::AnyCountChange).Remove(WeaponReuseCooldownTagChangedHandle);
	}
	WeaponReuseCooldownTagChangedHandle.Reset();
}

void UShooterCombatComponent::HandleWeaponReuseCooldownTagChanged(
	const FGameplayTag CooldownTag,
	int32 NewCount)
{
	(void)CooldownTag;
	(void)NewCount;
	RefreshCombatState();
}

void UShooterCombatComponent::ClearInputIntent()
{
	ClearPendingSprintExitFire();
	bWantsToAim = false;
	bWantsToFire = false;
	if (AShooterCharacter* ShooterCharacter = GetShooterCharacter())
	{
		if (AMeleeWeaponBase* MeleeWeapon = Cast<AMeleeWeaponBase>(ShooterCharacter->CurrentWeapon))
		{
			MeleeWeapon->ReleaseAttack();
		}
	}
}
