// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameFramework/Controller.h"
#include "Weapon/RangedWeaponBase.h"
#include "TimerManager.h"

UPartnerCombatComponent::UPartnerCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPartnerCombatComponent::TryStartAttack(bool bRequireFreshPress)
{
	const bool bCanTrack = PartnerCharacter && PartnerCharacter->CanTrackActionInputIntent();
	const uint8 BlockMask = (!PartnerCharacter ? 1 : 0) | ((bRequireFreshPress && bAttackNeedsFreshPress) ? 2 : 0)
		| (bAttackInputHeld ? 4 : 0) | (!bCanTrack ? 8 : 0);
	if (BlockMask != 0)
	{
		if (LastDebugAttackBlockMask != BlockMask)
		{
			UE_LOG(LogPartnerInputDebug, Display,
				TEXT("Event=Attack.Blocked Partner=%s CanTrack=%d RequireFresh=%d FreshRequired=%d Held=%d"),
				*GetNameSafe(PartnerCharacter), bCanTrack, bRequireFreshPress, bAttackNeedsFreshPress, bAttackInputHeld);
		}
		LastDebugAttackBlockMask = BlockMask;
		return;
	}
	LastDebugAttackBlockMask = 0;
	UE_LOG(LogPartnerInputDebug, Display, TEXT("Event=Attack.Accepted Partner=%s Authority=%d Epoch=%u"),
		*GetNameSafe(PartnerCharacter), PartnerCharacter->HasAuthority(), PartnerCharacter->GetActionInputEpoch());

	SendAttackInput(true);
}

void UPartnerCombatComponent::TryStopAttack()
{
	if (!PartnerCharacter)
	{
		return;
	}

	// Release는 재부팅/차단 중에도 전달해야 완료 시 과거 Press가 재개되지 않는다.
	SendAttackInput(false);
}

void UPartnerCombatComponent::SendAttackInput(bool bHeld)
{
	if (!PartnerCharacter) return;
	if (++NextAttackInputRequest == 0) ++NextAttackInputRequest;
	if (PartnerCharacter->HasAuthority())
	{
		ServerSetAttackHeld_Implementation(bHeld, NextAttackInputRequest, PartnerCharacter->GetActionInputEpoch());
	}
	else
	{
		bAttackInputHeld = bHeld;
		ServerSetAttackHeld(bHeld, NextAttackInputRequest, PartnerCharacter->GetActionInputEpoch());
	}
}

void UPartnerCombatComponent::ServerSetAttackHeld_Implementation(bool bHeld, uint32 Request, uint32 Epoch)
{
	UE_LOG(LogPartnerInputDebug, Display,
		TEXT("Event=Attack.ServerRequest Partner=%s Held=%d Request=%u Epoch=%u CurrentEpoch=%u ProcessedEpoch=%u ProcessedRequest=%u CanTrack=%d"),
		*GetNameSafe(PartnerCharacter), bHeld, Request, Epoch,
		PartnerCharacter ? PartnerCharacter->GetActionInputEpoch() : 0,
		ProcessedAttackInputEpoch, ProcessedAttackInputRequest,
		PartnerCharacter && PartnerCharacter->CanTrackActionInputIntent());
	if (!PartnerCharacter || Epoch != PartnerCharacter->GetActionInputEpoch() || Request == 0) return;
	// 순번은 새 세대에서만 초기화한다. 같은 세대의 실행 중단이 오래된 ON을 되살리면 안 된다.
	if (ProcessedAttackInputEpoch != Epoch)
	{
		ProcessedAttackInputEpoch = Epoch;
		ProcessedAttackInputRequest = 0;
	}
	if (static_cast<int32>(Request - ProcessedAttackInputRequest) <= 0) return;
	ProcessedAttackInputRequest = Request;
	if (bHeld && !PartnerCharacter->CanTrackActionInputIntent()) return;

	const bool bWasHeld = bAttackInputHeld;
	bAttackInputHeld = bHeld;
	if (!bHeld)
	{
		bAttackStartPending = false;
		ClearAttackRetry();
		StopCurrentAttack(bRebootSuspended);
		return;
	}
	if (!bWasHeld) bAttackStartPending = true;
	TryResumeAttack();
}

void UPartnerCombatComponent::LogAttackBlock(FName Reason)
{
	if (LastAttackBlockReason == Reason) return;
	LastAttackBlockReason = Reason;
	UE_LOG(LogTemp, Warning, TEXT("[PartnerWeaponVFX][Combat] Attack deferred Partner=%s Reason=%s Weapon=%s"),
		*GetNameSafe(PartnerCharacter), *Reason.ToString(),
		*GetNameSafe(PartnerCharacter ? PartnerCharacter->GetCurrentWeapon() : nullptr));
}

void UPartnerCombatComponent::TryResumeAttack()
{
	ClearAttackRetry();
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !bAttackInputHeld || !bAttackStartPending) return;
	const AController* Controller = PartnerCharacter->GetController();
	if (!Controller || Controller->GetPawn() != PartnerCharacter)
	{
		LogAttackBlock(TEXT("PawnOwnershipChanged"));
		ResetAttackInput();
		return;
	}
	if (!PartnerCharacter->CanTrackActionInputIntent())
	{
		ResetAttackInput();
		return;
	}
	if (bRebootSuspended || !PartnerCharacter->CanAcceptInput())
	{
		// 재부팅은 완료 콜백이 재평가한다. 차단 중에는 재시도 타이머를 돌리지 않는다.
		LogAttackBlock(TEXT("CanAcceptInput"));
		return;
	}

	AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon();
	if (Weapon && Weapon->IsAttacking())
	{
		bAttackStartPending = false;
		return;
	}
	if (Weapon && Weapon->CanAttack())
	{
		// 성공한 Press는 한 번만 소비한다. 단발 무기를 홀드 타이머로 연사시키지 않는다.
		// 첫 발에서 동기적으로 자동 재장전이 시작되면 그 경로가 Pending을 다시 설정한다.
		bAttackStartPending = false;
		LastAttackBlockReason = NAME_None;
		Weapon->StartAttack();
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(Weapon);
	LogAttackBlock(!Weapon ? TEXT("NoWeapon") : RangedWeapon ? RangedWeapon->GetAttackBlockReason() : TEXT("WeaponState"));
	if (RangedWeapon && RangedWeapon->IsReloading() && ReloadingWeapon.Get() == RangedWeapon)
	{
		// 이 컴포넌트가 시작한 재장전은 FinishReload에서 한 번만 재평가한다.
		return;
	}
	if (RangedWeapon && RangedWeapon->GetCurrentAmmo() == 0 && RangedWeapon->CanReload())
	{
		StartAutoReload();
		return;
	}
	if (GetWorld())
	{
		// 거부된 시작만 재시도한다. 연사 자체는 기존 Weapon 타이머가 계속 담당한다.
		GetWorld()->GetTimerManager().SetTimer(AttackRetryTimerHandle, this,
			&UPartnerCombatComponent::TryResumeAttack,
			RangedWeapon ? RangedWeapon->GetAttackRetryDelay() : 0.1f, false);
	}
}

void UPartnerCombatComponent::ClearAttackRetry()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(AttackRetryTimerHandle);
}

void UPartnerCombatComponent::StopCurrentAttack(bool bPreserveCadence)
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority()) return;
	AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon();
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(Weapon); RangedWeapon && bPreserveCadence)
	{
		// 재부팅 중 Release/입력 폐기도 기존 발사 간격을 지우지 않는다.
		RangedWeapon->PauseTargetAttack();
	}
	else if (Weapon)
	{
		Weapon->StopAttack();
	}
}

void UPartnerCombatComponent::ResetAttackInput()
{
	UE_LOG(LogPartnerInputDebug, Display, TEXT("Event=Attack.Reset Partner=%s FreshRequired=%d Held=%d"),
		*GetNameSafe(PartnerCharacter), bAttackNeedsFreshPress, bAttackInputHeld);
	bAttackNeedsFreshPress = true;
	bAttackStartPending = false;
	LastAttackBlockReason = NAME_None;
	ClearAttackRetry();
	if (PartnerCharacter && !PartnerCharacter->HasAuthority() && PartnerCharacter->IsLocallyControlled())
	{
		SendAttackInput(false);
	}
	else
	{
		bAttackInputHeld = false;
		StopCurrentAttack(bRebootSuspended);
	}
}

void UPartnerCombatComponent::StartAutoReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	ARangedWeaponBase* Weapon = Cast<ARangedWeaponBase>(PartnerCharacter->GetCurrentWeapon());
	if (!Weapon || !Weapon->CanReload())
	{
		return;
	}

	ClearAttackRetry();
	Weapon->StopAttack();
	Weapon->BeginReload();
	if (!Weapon->IsReloading())
	{
		return;
	}

	ReloadingWeapon = Weapon;
	bAttackStartPending = bAttackInputHeld;
	if (ReloadDurationSeconds <= 0.0f || !GetWorld())
	{
		FinishReload();
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimerHandle,
		this,
		&UPartnerCombatComponent::FinishReload,
		ReloadDurationSeconds,
		false);
}

void UPartnerCombatComponent::ForceStopAttack()
{
	ResetAttackInput();
}

void UPartnerCombatComponent::CancelForReboot()
{
	ResetAttackInput();
	CancelReload();
}

void UPartnerCombatComponent::SuspendForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority()) return;
	bRebootSuspended = true;
	bAttackStartPending = bAttackInputHeld;
	ClearAttackRetry();
	// 일반 StopAttack은 발사 대기까지 지운다. 재부팅은 남은 간격을 보존하는 중단 경로를 사용한다.
	StopCurrentAttack(true);
	CancelReload();
}

void UPartnerCombatComponent::CompleteReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !bRebootSuspended) return;
	bRebootSuspended = false;
	// 취소된 재장전은 이어가지 않는다. 현재 탄창을 채운 뒤에만 최신 홀드를 재평가한다.
	CancelReload();
	if (ARangedWeaponBase* Weapon = Cast<ARangedWeaponBase>(PartnerCharacter->GetCurrentWeapon()))
	{
		Weapon->RefillMagazine();
	}
	bAttackStartPending = bAttackInputHeld;
	TryResumeAttack();
}

void UPartnerCombatComponent::CancelReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	if (ARangedWeaponBase* Weapon = ReloadingWeapon.Get())
	{
		Weapon->CancelReload();
	}
	if (ARangedWeaponBase* CurrentRangedWeapon = Cast<ARangedWeaponBase>(PartnerCharacter->GetCurrentWeapon());
		CurrentRangedWeapon && CurrentRangedWeapon != ReloadingWeapon.Get())
	{
		CurrentRangedWeapon->CancelReload();
	}
	ReloadingWeapon.Reset();
}

void UPartnerCombatComponent::FinishReload()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		ReloadingWeapon.Reset();
		return;
	}

	ARangedWeaponBase* Weapon = ReloadingWeapon.Get();
	if (!PartnerCharacter->CanAcceptInput())
	{
		if (Weapon)
		{
			Weapon->CancelReload();
		}
		ReloadingWeapon.Reset();
		return;
	}

	if (Weapon && Weapon == PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->FinishReload();
	}
	else if (Weapon)
	{
		Weapon->CancelReload();
	}

	ReloadingWeapon.Reset();
	TryResumeAttack();
}

void UPartnerCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bAttackInputHeld = false;
	bAttackStartPending = false;
	ClearAttackRetry();
	CancelReload();
	StopCurrentAttack(false);
	Super::EndPlay(EndPlayReason);
}
