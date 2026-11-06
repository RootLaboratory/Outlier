// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerPlayerController.h"
#include "InputKeyEventArgs.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "LocalPlayerUISubSystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Room/RoomCombatSubsystem.h"
#include "TimerManager.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/EnemyHudWidget.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Attributes/OutlierPartnerCombatAttributeSet.h"
#include "GameplayTags/OutlierGameplayTags.h"

APartnerPlayerController::APartnerPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Partner;
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void APartnerPlayerController::FlushPressedKeys()
{
	if (APartnerCharacter* Partner = Cast<APartnerCharacter>(GetPawn())) Partner->ResetAccelerateInput();
	Super::FlushPressedKeys();
}

bool APartnerPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Enhanced Input의 재구성/Flush 콜백과 달리 실제 새 Press만 재입력을 허용한다.
	if (Params.Event == IE_Pressed)
	{
		if (APartnerCharacter* Partner = Cast<APartnerCharacter>(GetPawn())) Partner->NotifyActionKeyPressed(Params.Key);
	}
	return Super::InputKey(Params);
}

void APartnerPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void APartnerPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HackLifetimePendingRevealTarget.Reset();
	bHackPossessionRevealFinished = false;
	if (HasAuthority())
	{
		DiscardCommittedPartnerAbilityCooldownSession(CachedPartnerCharacter.Get());
	}
	if (HasAuthority() && PendingEnemyPossessionTarget.IsValid())
	{
		CancelPendingEnemyPossessionTransition();
	}

	AEnemyBase* ServerPendingTarget = PendingEnemyPossessionTarget.Get();
	AEnemyBase* LocalPendingTarget = LocalPendingEnemyPossessionTarget.Get();
	if (ServerPendingTarget)
	{
		ServerPendingTarget->OnEndPlay.RemoveDynamic(
			this,
			&APartnerPlayerController::HandlePossessionTargetEndPlay);
	}
	if (LocalPendingTarget && LocalPendingTarget != ServerPendingTarget)
	{
		LocalPendingTarget->OnEndPlay.RemoveDynamic(
			this,
			&APartnerPlayerController::HandlePossessionTargetEndPlay);
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->OnHackTransitionCovered.RemoveAll(this);
			PPSubsystem->OnHackTransitionFinished.RemoveAll(this);
			PPSubsystem->OnSplitPrismSettled.RemoveAll(this);
			PPSubsystem->CancelHackPossessionTransition();
		}
	}

	if (HasAuthority())
	{
		CancelPendingEnemyPossessionTransition();
	}

	if (IsLocalController())
	{
		LocalPendingEnemyPossessionTarget.Reset();
		bHackTransitionCoveredNotified = false;
		SetHackTransitionInputBlocked(false);
		PopEnemyHud();
	}

	UnbindShooterCharacterDelegates();
	UnbindPartnerVitalityDelegates();
	UnbindPlayerStateDelegates();
	OnPartnerPossessionStateChanged.Clear();
	Super::EndPlay(EndPlayReason);
}

void APartnerPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindPlayerStateDelegates();
	RefreshPartnerSuitUI();
	RefreshShooterUIForRespawnFromPlayerState();
}

void APartnerPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}

	if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(InPawn))
	{
		PartnerCharacter->Tags.Add(PartnerPawnTag);

		if (IsLocalController())
		{
			SetPartnerPossessionState(
				EPartnerPossessionState::PartnerControlled,
				PartnerCharacter);
		}
	}

	/*if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(InPawn))
	{

		if (IsLocalController())
		{
			UE_LOG(LogTemp, Error, TEXT("TryRefresh"));
			PartnerCharacter->RefreshPostProcessState();
		}
	}*/

	if (IsLocalController())
	{
		BindPlayerStateDelegates();
		RefreshPartnerSuitUI();

		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}

		TryRestoreFirstPersonDefaultInputMode(FirstPersonInputModeTags::Hack());
		BeginLocalEnemyPossessionReveal(InPawn);
	}
}

void APartnerPlayerController::PawnPendingDestroy(APawn* InPawn)
{
	AEnemyBase* EnemyPawn = Cast<AEnemyBase>(InPawn);
	if (HasAuthority() && EnemyPawn && InPawn == GetPawn() && CachedPartnerCharacter.IsValid())
	{
		EnemyPawn->ClearPossessedPlayerState();
		RestoreCachedPartnerCharacterNextTick();
		return;
	}

	Super::PawnPendingDestroy(InPawn);
}

void APartnerPlayerController::CachePartnerCharacterForEnemyPossession(APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority() || !PartnerCharacter)
	{
		return;
	}

	CachedPartnerCharacter = PartnerCharacter;
}

//Enemy의 HandleHack 이후부터 Controller가 책임.
bool APartnerPlayerController::BeginEnemyPossessionTransition(
	AEnemyBase* EnemyTarget,
	APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority()
		|| !IsValid(EnemyTarget)
		|| !IsValid(PartnerCharacter)
		|| EnemyTarget->IsActorBeingDestroyed()
		|| EnemyTarget->IsDead()
		|| EnemyTarget->GetCurrentHealth() <= 0.0f
		|| !EnemyTarget->IsPossessionInProgress()
		|| !PartnerCharacter->CanAcceptInput()
		|| GetPawn() != PartnerCharacter
		|| EnemyTarget->IsEnemyPossessed())
	{
		return false;
	}

	if (PendingEnemyPossessionTarget.IsValid() || PendingEnemyPossessionSource.IsValid())
	{
		return false;
	}

	PendingEnemyPossessionTarget = EnemyTarget;
	PendingEnemyPossessionSource = PartnerCharacter;
	BindPossessionTargetEndPlay(EnemyTarget); //Transitioning 중에 Enemy EndPlay 호출 시, 연출 및 해킹 종료.

	ClientBeginEnemyPossessionTransition(EnemyTarget);
	return true;
}

void APartnerPlayerController::ClientBeginEnemyPossessionTransition_Implementation(AEnemyBase* EnemyTarget)
{
	if (!IsLocalController() || !IsValid(EnemyTarget))
	{
		return;
	}

	LocalPendingEnemyPossessionTarget = EnemyTarget;
	BindPossessionTargetEndPlay(EnemyTarget);
	bHackTransitionCoveredNotified = false;
	SetFirstPersonInputMode(FirstPersonInputModeTags::Hack());
	SetPartnerPossessionState(
		EPartnerPossessionState::Transitioning,
		EnemyTarget);
}

void APartnerPlayerController::HandlePartnerHackPossessionTransition()
{
	if (!IsLocalController() || !LocalPendingEnemyPossessionTarget.IsValid())
	{
		return;
	}

	BindPostProcessSubSystem();

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->StartHackPossessionTransition();
			return;
		}
	}

	NotifyHackTransitionCovered();
}

void APartnerPlayerController::NotifyHackTransitionCovered()
{
	if (!IsLocalController() || bHackTransitionCoveredNotified)
	{
		return;
	}

	AEnemyBase* EnemyTarget = LocalPendingEnemyPossessionTarget.Get();
	if (!IsValid(EnemyTarget))
	{
		return;
	}

	bHackTransitionCoveredNotified = true;
	ServerNotifyHackTransitionCovered(EnemyTarget);
}

void APartnerPlayerController::ServerNotifyHackTransitionCovered_Implementation(AEnemyBase* EnemyTarget)
{
	if (!HasAuthority() || PendingEnemyPossessionTarget.Get() != EnemyTarget)
	{
		ClientCancelEnemyPossessionTransition(EnemyTarget);
		return;
	}

	CommitPendingEnemyPossession(EnemyTarget);
}

void APartnerPlayerController::CommitPendingEnemyPossession(AEnemyBase* ExpectedTarget)
{
	AEnemyBase* EnemyTarget = PendingEnemyPossessionTarget.Get();
	APartnerCharacter* PartnerCharacter = PendingEnemyPossessionSource.Get();

	// 화면을 가리는 동안 대상이 사망하거나 빙의 대기가 취소될 수 있다.
	// 클라이언트의 연출 완료 통보는 생존 보장이 아니므로 Possess 직전에 서버에서 재검증한다.
	if (!HasAuthority()
		|| EnemyTarget != ExpectedTarget
		|| !IsValid(EnemyTarget)
		|| !IsValid(PartnerCharacter)
		|| EnemyTarget->IsActorBeingDestroyed()
		|| EnemyTarget->IsDead()
		|| EnemyTarget->GetCurrentHealth() <= 0.0f
		|| !EnemyTarget->IsPossessionInProgress()
		|| !PartnerCharacter->CanAcceptInput()
		|| GetPawn() != PartnerCharacter
		|| EnemyTarget->IsEnemyPossessed())
	{
		CancelPendingEnemyPossessionTransition();
		return;
	}

	CachePartnerCharacterForEnemyPossession(PartnerCharacter);
	// Listen Server에서는 Possess 안에서 연출 없는 복귀 콜백이 즉시 올 수 있다.
	// 먼저 대상을 기록하고, 실제 타이머 시작은 아래 commit과 연출 완료를 모두 확인한다.
	HackLifetimePendingRevealTarget = EnemyTarget;
	bHackPossessionRevealFinished = false;
	Possess(EnemyTarget);

	if (GetPawn() != EnemyTarget)
	{
		HackLifetimePendingRevealTarget.Reset();
		CachedPartnerCharacter.Reset();
		CancelPendingEnemyPossessionTransition();
		return;
	}

	if (!BeginCommittedPartnerAbilityCooldownSession(PartnerCharacter))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerPossession] Failed to suspend Partner cooldowns after possession commit Partner=%s Enemy=%s"),
			*GetNameSafe(PartnerCharacter),
			*GetNameSafe(EnemyTarget));
		ReleaseEnemyPossessionForCleanup();
		PendingEnemyPossessionTarget.Reset();
		PendingEnemyPossessionSource.Reset();
		UnbindPossessionTargetEndPlay(EnemyTarget);
		return;
#else
		checkf(
			false,
			TEXT("[PartnerPossession] Failed to suspend Partner cooldowns after possession commit Partner=%s Enemy=%s"),
			*GetNameSafe(PartnerCharacter),
			*GetNameSafe(EnemyTarget));
#endif
	}

	// PossessedBy에서는 아직 실패 복귀가 가능하다. 성공 확정 뒤에만 기존 스턴을 해제한다.
	EnemyTarget->ClearStunForCommittedPossession();

	// 실제 빙의와 쿨타임 세션이 확정된 시점의 값만 복사한다.
	if (const UOutlierAbilitySystemComponent* PartnerASC = PartnerCharacter->GetOutlierAbilitySystemComponent())
	{
		EnemyTarget->SetPossessionDamageMultiplier(PartnerASC->GetNumericAttribute(
			UOutlierPartnerCombatAttributeSet::GetHackedDroneDamageMultiplierAttribute()));
	}

	PendingEnemyPossessionTarget.Reset();
	PendingEnemyPossessionSource.Reset();
	UnbindPossessionTargetEndPlay(EnemyTarget);

	TryStartHackLifetimeAfterReveal();
	// 실제 빙의와 능력 세션이 모두 성공 확정된 뒤에만 적 생존 집계에서 제외한다.
	if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		Combat->NotifyEnemyAllied(EnemyTarget);
	}
}

void APartnerPlayerController::ServerNotifyHackPossessionRevealFinished_Implementation(AEnemyBase* EnemyTarget)
{
	if (!HasAuthority() || !IsValid(EnemyTarget)
		|| HackLifetimePendingRevealTarget.Get() != EnemyTarget || GetPawn() != EnemyTarget
		|| !EnemyTarget->IsEnemyPossessed() || EnemyTarget->IsDead()
		|| EnemyTarget->GetCurrentHealth() <= 0.0f || EnemyTarget->IsActorBeingDestroyed())
	{
		return;
	}
	bHackPossessionRevealFinished = true;
	TryStartHackLifetimeAfterReveal();
}

void APartnerPlayerController::TryStartHackLifetimeAfterReveal()
{
	AEnemyBase* EnemyTarget = HackLifetimePendingRevealTarget.Get();
	if (!HasAuthority() || !bHackPossessionRevealFinished || !bPartnerAbilityCooldownSessionCommitted
		|| !IsValid(EnemyTarget) || GetPawn() != EnemyTarget)
	{
		return;
	}
	// 화면 복귀가 끝난 뒤 서버가 한 번만 시작한다. 늦거나 중복된 완료 통보는 재시작할 수 없다.
	HackLifetimePendingRevealTarget.Reset();
	bHackPossessionRevealFinished = false;
	if (!EnemyTarget->BeginHackEffect())
	{
		ReleaseEnemyPossessionForCleanup();
	}
}

bool APartnerPlayerController::ShouldDeferPartnerHackRecharge(const APartnerCharacter* PartnerCharacter) const
{
	return PartnerCharacter && (PendingEnemyPossessionSource.Get() == PartnerCharacter
		|| bPartnerAbilityCooldownSessionCommitted);
}

void APartnerPlayerController::CancelPendingEnemyPossessionTransition()
{
	if (!HasAuthority())
	{
		return;
	}

	AEnemyBase* EnemyTarget = PendingEnemyPossessionTarget.Get();
	if (EnemyTarget)
	{
		EnemyTarget->CancelPossessionProcess();
	}
	else if (APartnerCharacter* PartnerCharacter = PendingEnemyPossessionSource.Get())
	{
		PartnerCharacter->SetEnemyPossessionProtection(false);
	}

	PendingEnemyPossessionTarget.Reset();
	if (APartnerCharacter* PartnerCharacter = PendingEnemyPossessionSource.Get())
	{
		if (UOutlierAbilitySystemComponent* AbilitySystem = PartnerCharacter->GetOutlierAbilitySystemComponent())
		{
			AbilitySystem->CancelPartnerHackDeferredRecharge();
		}
	}
	PendingEnemyPossessionSource.Reset();
	UnbindPossessionTargetEndPlay(EnemyTarget);
	ClientCancelEnemyPossessionTransition(EnemyTarget);
}

void APartnerPlayerController::ClientCancelEnemyPossessionTransition_Implementation(AEnemyBase* EnemyTarget)
{
	CancelLocalEnemyPossessionTransition(EnemyTarget);
}

void APartnerPlayerController::CancelLocalEnemyPossessionTransition(AEnemyBase* ExpectedTarget)
{
	if (!IsLocalController())
	{
		return;
	}

	if (ExpectedTarget && LocalPendingEnemyPossessionTarget.Get() != ExpectedTarget)
	{
		return;
	}

	AEnemyBase* LocalTarget = LocalPendingEnemyPossessionTarget.Get();
	LocalPendingEnemyPossessionTarget.Reset();
	UnbindPossessionTargetEndPlay(LocalTarget);
	bHackTransitionCoveredNotified = false;

	TryRestoreFirstPersonDefaultInputMode(FirstPersonInputModeTags::Hack());
	SetPartnerPossessionState(
		EPartnerPossessionState::PartnerControlled,
		ExpectedTarget ? ExpectedTarget : LocalTarget);
}

void APartnerPlayerController::BeginLocalEnemyPossessionReveal(APawn* InAcknowledgedPawn)
{
	if (!IsLocalController()
		|| !InAcknowledgedPawn
		|| LocalPendingEnemyPossessionTarget.Get() != InAcknowledgedPawn)
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			if (PPSubsystem->StartHackPossessionReveal())
			{
				return;
			}
		}
	}

	HandleHackPossessionTransitionFinished();
}

void APartnerPlayerController::HandleHackPossessionTransitionFinished()
{
	if (!IsLocalController()
		|| PartnerPossessionState != EPartnerPossessionState::Transitioning)
	{
		return;
	}

	AEnemyBase* CompletedTarget = LocalPendingEnemyPossessionTarget.Get();
	if (!IsValid(CompletedTarget) || GetPawn() != CompletedTarget)
	{
		CancelLocalEnemyPossessionTransition(CompletedTarget);
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[PartnerPossession] Transition finished Target=%s"),
		*GetNameSafe(CompletedTarget));

	LocalPendingEnemyPossessionTarget.Reset();
	UnbindPossessionTargetEndPlay(CompletedTarget);
	bHackTransitionCoveredNotified = false;
	TryRestoreFirstPersonDefaultInputMode(FirstPersonInputModeTags::Hack());
	SetPartnerPossessionState(
		EPartnerPossessionState::EnemyPossessed,
		CompletedTarget);
	NotifyHackPossessionPresentationFinished();
}

void APartnerPlayerController::NotifyHackPossessionPresentationFinished()
{
	if (!IsLocalController() || PartnerPossessionState != EPartnerPossessionState::EnemyPossessed)
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
			LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			// 화면 복귀 뒤 시작하는 초점 연출까지 기다린다. 상시 잔차(Holding)는 더 기다리지 않는다.
			const ESplitPrismPhase Phase = PPSubsystem->GetSplitPrismPhase();
			if (Phase == ESplitPrismPhase::Focusing || Phase == ESplitPrismPhase::Settling)
			{
				return;
			}
		}
	}
	if (AEnemyBase* EnemyTarget = Cast<AEnemyBase>(GetPawn()))
	{
		ServerNotifyHackPossessionRevealFinished(EnemyTarget);
	}
}

void APartnerPlayerController::SetPartnerPossessionState(
	EPartnerPossessionState NewState,
	AActor* ContextActor)
{
	if (!IsLocalController() || PartnerPossessionState == NewState)
	{
		return;
	}

	const EPartnerPossessionState PreviousState = PartnerPossessionState;
	PartnerPossessionState = NewState;

	switch (PartnerPossessionState)
	{
	case EPartnerPossessionState::Transitioning:
		SetHackTransitionInputBlocked(true);
		HandlePartnerHackPossessionTransition();
		break;

	case EPartnerPossessionState::PartnerControlled:
		SetHackTransitionInputBlocked(false);
		RequestPopEnemyHud();
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
				LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
			{
				PPSubsystem->CancelHackPossessionTransition();
				PPSubsystem->StopSplitPrism();
			}
		}
		break;

	case EPartnerPossessionState::EnemyPossessed:
		SetHackTransitionInputBlocked(false);
		PushEnemyHud(Cast<AEnemyBase>(ContextActor));
		// 드론 시야로 넘어온 직후 초점이 맞춰지는 연출. 사망 중이면 아래 RefreshHudVisibility가 바로 끈다.
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
				LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
			{
				PPSubsystem->StartSplitPrism();
			}
		}
		break;
	}

	// Transitioning 진입에서 MainWidget을 접고, PartnerControlled 복귀에서 편다.
	RefreshHudVisibility();

	OnPartnerPossessionStateChanged.Broadcast(
		PreviousState,
		PartnerPossessionState,
		ContextActor);
}

void APartnerPlayerController::SetHackTransitionInputBlocked(bool bBlocked)
{
	if (!IsLocalController() || bHackTransitionInputBlocked == bBlocked)
	{
		return;
	}

	bHackTransitionInputBlocked = bBlocked;
	SetIgnoreMoveInput(bBlocked);
	SetIgnoreLookInput(bBlocked);

	if (bBlocked)
	{
		FlushPressedKeys();
	}
}

void APartnerPlayerController::RefreshHudVisibility()
{
	Super::RefreshHudVisibility();
	if (bEnemyHudPopPending && IsHudHiddenForDeath())
	{
		PopEnemyHud();
	}
	EnemyHudCollapseState.Apply(EnemyHudInstance, !IsHudHiddenForDeath());
	if (EnemyHudInstance)
	{
		EnemyHudInstance->SetDamageFeedbackSuppressed(IsHudHiddenForDeath());
	}

	// 사망 연출과 겹치지 않게 Split Prism도 같이 끈다.
	if (IsHudHiddenForDeath())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>()
			: nullptr)
		{
			PPSubsystem->StopSplitPrism();
		}
	}
}

bool APartnerPlayerController::ShouldShowMainWidget() const
{
	if (!Super::ShouldShowMainWidget())
	{
		return false;
	}

	switch (PartnerPossessionState)
	{
	case EPartnerPossessionState::PartnerControlled:
		return true;

	case EPartnerPossessionState::EnemyPossessed:
		// Enemy HUD를 못 띄웠으면(WBP 미지정 등) Partner HUD를 남긴다.
		return !EnemyHudInstance;

	default:
		return false;
	}
}

void APartnerPlayerController::PushEnemyHud(AEnemyBase* PossessedEnemy)
{
	// 빠른 재빙의는 이전 HUD의 지연 제거를 기다리지 않는다. 새 대상에 옛 콜백을 남기지 않는다.
	if (bEnemyHudPopPending)
	{
		PopEnemyHud();
	}
	if (!IsLocalController() || EnemyHudLayerHandle.IsValid() || !IsValid(PossessedEnemy))
	{
		return;
	}

	const TSubclassOf<UEnemyHudWidget> HudClass = PossessedEnemy->GetPossessedHudClass();
	if (!HudClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPossession] PossessedHudClass is not set on %s"),
			*GetNameSafe(PossessedEnemy->GetClass()));
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	UEnemyHudWidget* EnemyHud = CreateWidget<UEnemyHudWidget>(this, HudClass);
	if (!EnemyHud)
	{
		return;
	}
	EnemyHud->InitializeForEnemy(PossessedEnemy);

	// 입력을 받지 않는 HUD라 InputModeTag는 적용되지 않는다.
	EnemyHudLayerHandle = LayerSubsystem->PushWidget(
		UILayerTags::Gameplay(),
		EnemyHud,
		FirstPersonInputModeTags::UI(),
		this,
		EUILayerFocusTarget::None,
		false,
		false);
	if (!EnemyHudLayerHandle.IsValid())
	{
		EnemyHud->ReleaseDamageFeedback();
		return;
	}

	EnemyHudInstance = EnemyHud;
}

void APartnerPlayerController::ClientPrepareExpiredEnemyHud_Implementation(AEnemyBase* EnemyTarget)
{
	if (EnemyHudInstance)
	{
		EnemyHudInstance->ShowHackTimerExpiry(EnemyTarget);
	}
}

void APartnerPlayerController::RequestPopEnemyHud()
{
	if (EnemyHudInstance && EnemyHudInstance->IsShowingHackTimerExpiry() && !IsHudHiddenForDeath())
	{
		// 제어/입력은 이미 Partner로 복귀한다. 체력 피드백은 즉시 끊고 마지막 숫자만 한 프레임 남긴다.
		bEnemyHudPopPending = true;
		EnemyHudInstance->ReleaseDamageFeedback(true);
		EnemyHudInstance->OnHackTimerExpiryDisplayFinished.BindUObject(this, &ThisClass::PopEnemyHud);
		return;
	}
	PopEnemyHud();
}

void APartnerPlayerController::PopEnemyHud()
{
	bEnemyHudPopPending = false;
	if (EnemyHudInstance)
	{
		EnemyHudInstance->OnHackTimerExpiryDisplayFinished.Unbind();
		EnemyHudInstance->ReleaseDamageFeedback();
	}

	if (EnemyHudLayerHandle.IsValid())
	{
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>()
			: nullptr)
		{
			LayerSubsystem->PopLayer(EnemyHudLayerHandle);
		}
	}

	EnemyHudLayerHandle.Reset();
	EnemyHudInstance = nullptr;
	EnemyHudCollapseState.Reset();
}

void APartnerPlayerController::BindPossessionTargetEndPlay(AEnemyBase* EnemyTarget)
{
	if (!IsValid(EnemyTarget))
	{
		return;
	}

	EnemyTarget->OnEndPlay.RemoveDynamic(
		this,
		&APartnerPlayerController::HandlePossessionTargetEndPlay);

	EnemyTarget->OnEndPlay.AddDynamic(
		this,
		&APartnerPlayerController::HandlePossessionTargetEndPlay);
}

void APartnerPlayerController::UnbindPossessionTargetEndPlay(AEnemyBase* EnemyTarget)
{
	if (!EnemyTarget
		|| PendingEnemyPossessionTarget.Get() == EnemyTarget
		|| LocalPendingEnemyPossessionTarget.Get() == EnemyTarget)
	{
		return;
	}

	EnemyTarget->OnEndPlay.RemoveDynamic(
		this,
		&APartnerPlayerController::HandlePossessionTargetEndPlay);
}

void APartnerPlayerController::HandlePossessionTargetEndPlay(
	AActor* EndedActor,
	EEndPlayReason::Type EndPlayReason)
{
	AEnemyBase* EndedEnemy = Cast<AEnemyBase>(EndedActor);
	if (!EndedEnemy)
	{
		return;
	}

	const bool bServerTransitionTarget =
		PendingEnemyPossessionTarget.Get() == EndedEnemy;

	const bool bLocalTransitionTarget =
		LocalPendingEnemyPossessionTarget.Get() == EndedEnemy;

	if (!bServerTransitionTarget && !bLocalTransitionTarget)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[PartnerPossession] Transition target ended Target=%s EndPlayReason=%d"),
		*GetNameSafe(EndedEnemy),
		static_cast<int32>(EndPlayReason));

	if (HasAuthority() && bServerTransitionTarget)
	{
		CancelPendingEnemyPossessionTransition();
	}

	if (IsLocalController()
		&& LocalPendingEnemyPossessionTarget.Get() == EndedEnemy)
	{
		CancelLocalEnemyPossessionTransition(EndedEnemy);
	}
}

void APartnerPlayerController::ReleaseEnemyPossession()
{
	if (!HasAuthority())
	{
		ServerReleaseEnemyPossession();
		return;
	}
	if (AEnemyBase* EnemyPawn = HackLifetimePendingRevealTarget.Get();
		EnemyPawn && GetPawn() == EnemyPawn && !EnemyPawn->IsDead())
	{
		// 제어 전환 후 연출 도중 해제도 수동 종료 처치다. 종료 사유만 남기고 즉시 끝낸다.
		EnemyPawn->BeginHackEffect();
	}
	if (AEnemyBase* EnemyPawn = Cast<AEnemyBase>(GetPawn()); EnemyPawn && EnemyPawn->IsHackEffectActive())
	{
		// Die -> PerformDeathCleanup에서 다시 호출될 때는 아래 복귀만 실행한다.
		EnemyPawn->EndHackEffectAndDie(EEnemyHackEffectEndReason::ManualRelease);
		return;
	}
	RestoreEnemyPossessionWithoutKill();
}

void APartnerPlayerController::ReleaseEnemyPossessionForCleanup()
{
	if (!HasAuthority())
	{
		return;
	}
	if (AEnemyBase* EnemyPawn = Cast<AEnemyBase>(GetPawn()))
	{
		EnemyPawn->EndHackEffect(EEnemyHackEffectEndReason::Cleanup);
	}
	RestoreEnemyPossessionWithoutKill();
}

void APartnerPlayerController::ReleaseEnemyPossessionForDisconnect()
{
	if (!HasAuthority())
	{
		return;
	}
	// 연출 완료 통보 전에 끊긴 연결은 더 이상 콜백을 보낼 수 없다. 무기한 생존하지 않도록
	// 이 경우에만 서버에서 시간을 시작하고, 이미 시작한 효과의 만료 시각은 그대로 유지한다.
	if (AEnemyBase* EnemyPawn = HackLifetimePendingRevealTarget.Get();
		EnemyPawn && GetPawn() == EnemyPawn && !EnemyPawn->IsDead())
	{
		EnemyPawn->BeginHackEffect();
	}
	// 연결만 끊긴 경우 현재 Pawn 복구 정책을 유지하되, 대상의 만료 타이머는 취소하지 않는다.
	RestoreEnemyPossessionWithoutKill();
}

void APartnerPlayerController::RestoreEnemyPossessionWithoutKill()
{
	if (!HasAuthority())
	{
		return;
	}
	HackLifetimePendingRevealTarget.Reset();
	bHackPossessionRevealFinished = false;

	AEnemyBase* EnemyPawn = Cast<AEnemyBase>(GetPawn());
	if (!EnemyPawn)
	{
		return;
	}

	// Pooled enemies do not trigger PawnPendingDestroy on death.
	if (EnemyPawn->IsDead() || EnemyPawn->GetCurrentHealth() <= 0.0f
		|| EnemyPawn->HasHackEffectEndedWithDeath())
	{
		// Enemy의 종료 multicast와 Controller의 Pawn 복제는 서로 다른 채널이다.
		// ClientRestart보다 먼저 같은 Controller 채널에 알림을 보내 HUD 제거 순서를 고정한다.
		if (EnemyPawn->HasHackEffectExpired())
		{
			ClientPrepareExpiredEnemyHud(EnemyPawn);
		}
		UnPossess();
		RestoreCachedPartnerCharacter();
		return;
	}

	if (!CachedPartnerCharacter.IsValid())
	{
		DiscardCommittedPartnerAbilityCooldownSession(nullptr);
		if (AController* CachedAIController = EnemyPawn->GetCachedAIController())
		{
			CachedAIController->Possess(EnemyPawn);
		}
		else
		{
			EnemyPawn->SetEnemyPossessed(false);
		}

		CachedPartnerCharacter.Reset();
		return;
	}

	if (AController* CachedAIController = EnemyPawn->GetCachedAIController())
	{
		CachedAIController->Possess(EnemyPawn);
	}
	else
	{
		EnemyPawn->SetEnemyPossessed(false);
	}

	RestoreCachedPartnerCharacter();
}

bool APartnerPlayerController::RestorePartnerForWorldTransition()
{
	if (!HasAuthority())
	{
		return false;
	}
	// 아직 Possess 전이면 Release만으로 취소되지 않는다. 먼저 지연 전환을 끊어
	// 출구 이동 뒤 늦은 해킹 콜백이 Enemy로 다시 빙의시키는 경로를 막는다.
	if (PendingEnemyPossessionTarget.IsValid() || PendingEnemyPossessionSource.IsValid())
	{
		CancelPendingEnemyPossessionTransition();
	}
	ReleaseEnemyPossessionForCleanup();
	const AOutlierPlayerState* PS = GetPlayerState<AOutlierPlayerState>();
	return PS && IsValid(PS->GetPartnerCharacter()) && GetPawn() == PS->GetPartnerCharacter();
}

void APartnerPlayerController::ServerReleaseEnemyPossession_Implementation()
{
	ReleaseEnemyPossession();
}

APartnerCharacter* APartnerPlayerController::ExtractCachedPartnerCharacterForLogout()
{
	APartnerCharacter* Result = CachedPartnerCharacter.Get();
	DiscardCommittedPartnerAbilityCooldownSession(Result);
	CachedPartnerCharacter.Reset();
	return Result;
}

void APartnerPlayerController::RestoreCachedPartnerCharacter()
{
	if (!HasAuthority())
	{
		return;
	}

	APartnerCharacter* PartnerCharacter = CachedPartnerCharacter.Get();
	if (!PartnerCharacter)
	{
		DiscardCommittedPartnerAbilityCooldownSession(nullptr);
		CachedPartnerCharacter.Reset();
		return;
	}

	PartnerCharacter->SetEnemyPossessionProtection(false);
	Possess(PartnerCharacter);
	if (GetPawn() == PartnerCharacter)
	{
		const bool bFinalized = FinalizeCommittedPartnerAbilityCooldownSession(PartnerCharacter);
#if UE_BUILD_SHIPPING
		if (!bFinalized)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[PartnerPossession] Failed to restore Partner cooldown session Partner=%s"),
				*GetNameSafe(PartnerCharacter));
		}
#else
		checkf(
			bFinalized,
			TEXT("[PartnerPossession] Failed to restore Partner cooldown session Partner=%s"),
			*GetNameSafe(PartnerCharacter));
#endif
	}
	else
	{
		DiscardCommittedPartnerAbilityCooldownSession(PartnerCharacter);
	}
	CachedPartnerCharacter.Reset();
}

bool APartnerPlayerController::BeginCommittedPartnerAbilityCooldownSession(
	APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority() || !IsValid(PartnerCharacter) || bPartnerAbilityCooldownSessionCommitted)
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->SuspendPartnerSkillCooldownsForPossession())
	{
		return false;
	}

	bPartnerAbilityCooldownSessionCommitted = true;
	return true;
}

bool APartnerPlayerController::FinalizeCommittedPartnerAbilityCooldownSession(
	APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority() || !bPartnerAbilityCooldownSessionCommitted)
	{
		return false;
	}

	bPartnerAbilityCooldownSessionCommitted = false;
	if (!IsValid(PartnerCharacter))
	{
		return false;
	}

	UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return false;
	}

	const bool bResumed = AbilitySystem->ResumePartnerSkillCooldownsAfterPossession();
	// 소모는 해킹 결과에서 끝났다. 복귀 시에는 정지된 충전만 이어가며 다시 소모하지 않는다.
	return bResumed;
}

void APartnerPlayerController::DiscardCommittedPartnerAbilityCooldownSession(
	APartnerCharacter* PartnerCharacter)
{
	if (!HasAuthority() || !bPartnerAbilityCooldownSessionCommitted)
	{
		return;
	}

	bPartnerAbilityCooldownSessionCommitted = false;
	if (IsValid(PartnerCharacter))
	{
		if (UOutlierAbilitySystemComponent* AbilitySystem =
			PartnerCharacter->GetOutlierAbilitySystemComponent())
		{
			AbilitySystem->DiscardSuspendedPartnerSkillCooldowns();
		}
	}
}

void APartnerPlayerController::RestoreCachedPartnerCharacterNextTick()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	GetWorldTimerManager().SetTimerForNextTick(
		this,
		&APartnerPlayerController::RestoreCachedPartnerCharacter
	);
}

void APartnerPlayerController::BindMainUI()
{
	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI skipped: not local PC=%s Auth=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (ShooterUIInstance)
	{
		BindPlayerStateDelegates();
		RefreshPartnerSuitUI();
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI skipped: already exists PC=%s UI=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ShooterUIInstance));
		return;
	}

	if (!MainUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI failed: MainUIClass is null PC=%s Class=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetClass()));
		return;
	}

	ShooterUIInstance = CreateWidget<UMainUIBase>(this, MainUIClass);
	if (!ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI failed: CreateWidget returned null PC=%s MainUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(MainUIClass));
		return;
	}

	ShooterUIInstance->AddToViewport();
	BindPlayerStateDelegates();
	// RegisterMainUI가 이전 캐시를 적용하기 전에 현재 자기 PS 값으로 맞춘다.
	RefreshPartnerSuitUI();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerPC] No GetLocalPlayer"));
		}

		if (ULocalPlayerUILayerSubsystem* LayerSubsystem =
			LP->GetSubsystem<ULocalPlayerUILayerSubsystem>())
		{
			LayerSubsystem->RegisterMainUI(ShooterUIInstance);
		}
	}
	RefreshPartnerSuitUI();
}

void APartnerPlayerController::BindPostProcessSubSystem()
{
	if (!IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}

	if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
		LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
	{
		PPSubsystem->OnHackTransitionCovered.RemoveAll(this);
		PPSubsystem->OnHackTransitionFinished.RemoveAll(this);
		PPSubsystem->OnSplitPrismSettled.RemoveAll(this);
		PPSubsystem->OnHackTransitionCovered.AddUObject(
			this,
			&APartnerPlayerController::NotifyHackTransitionCovered);
		PPSubsystem->OnHackTransitionFinished.AddUObject(
			this,
			&APartnerPlayerController::HandleHackPossessionTransitionFinished);
		PPSubsystem->OnSplitPrismSettled.AddUObject(
			this,
			&APartnerPlayerController::NotifyHackPossessionPresentationFinished);
	}
}

void APartnerPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	if (!IsLocalController())
	{
		return;
	}

	BindMainUI();
	if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetPawn()))
	{
		BindPartnerVitalityDelegates(PartnerCharacter);
	}
	BindPostProcessSubSystem();
}

void APartnerPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	if (IsLocalController() && Cast<APartnerCharacter>(P))
	{
		SetPartnerPossessionState(
			EPartnerPossessionState::PartnerControlled,
			P);
	}

	if (AEnemyBase* PossessedEnemy = Cast<AEnemyBase>(P);
		IsLocalController()
		&& IsValid(PossessedEnemy)
		&& LocalPendingEnemyPossessionTarget.Get() == PossessedEnemy)
	{
		TryRestoreFirstPersonDefaultInputMode(FirstPersonInputModeTags::Hack());
	}

	BindMainUI();
	if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(P))
	{
		BindPartnerVitalityDelegates(PartnerCharacter);
	}
	BindPostProcessSubSystem();
	BindPlayerStateDelegates();
	RefreshPartnerSuitUI();
	RefreshShooterUIForRespawnFromPlayerState();

	if (IsLocalController())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}
	}

	//적 빙의 이후, Transition Property trigger;
	BeginLocalEnemyPossessionReveal(P);
}

void APartnerPlayerController::RefreshShooterUIForRespawnFromPlayerState()
{
	BindShooterCharacterDelegatesFromPlayerState();
}

void APartnerPlayerController::RefreshPartnerSuitUI()
{
	if (!IsLocalController())
	{
		return;
	}

	const AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem();
	if (UISubsystem)
	{
		UISubsystem->OnShooterSuitAcquiredChanged(
			OutlierPlayerState && OutlierPlayerState->GetAcquiredSuit());
	}
	// HUD 재등록 뒤에는 복제를 다시 기다리지 않고 현재 충전 상태를 고정 슬롯에 반영한다.
	if (APartnerCharacter* Partner = Cast<APartnerCharacter>(GetPawn()))
	{
		Partner->RefreshPartnerHackChargeUI();
	}
}

void APartnerPlayerController::BindPartnerVitalityDelegates(APartnerCharacter* PartnerCharacter)
{
	UnbindPartnerVitalityDelegates();

	if (!IsLocalController() || !PartnerCharacter)
	{
		return;
	}

	BoundPartnerAbilitySystem = PartnerCharacter->GetOutlierAbilitySystemComponent();

	if (BoundPartnerAbilitySystem)
	{
		PartnerHealthChangedHandle = BoundPartnerAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
				this, &APartnerPlayerController::HandlePartnerHealthAttributeChanged);
		PartnerMaxHealthChangedHandle = BoundPartnerAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).AddUObject(
				this, &APartnerPlayerController::HandlePartnerHealthAttributeChanged);
	}

	RefreshPartnerVitalityUI();
}

void APartnerPlayerController::UnbindPartnerVitalityDelegates()
{
	if (BoundPartnerAbilitySystem)
	{
		BoundPartnerAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(PartnerHealthChangedHandle);
		BoundPartnerAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).Remove(PartnerMaxHealthChangedHandle);
	}

	PartnerHealthChangedHandle.Reset();
	PartnerMaxHealthChangedHandle.Reset();
	BoundPartnerAbilitySystem = nullptr;
}

void APartnerPlayerController::RefreshPartnerVitalityUI()
{
	if (BoundPartnerAbilitySystem)
	{
		HandlePartnerHealthChanged(
			BoundPartnerAbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetHealthAttribute()),
			BoundPartnerAbilitySystem->GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute()));
	}
}

void APartnerPlayerController::HandlePartnerHealthAttributeChanged(
	const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	RefreshPartnerVitalityUI();
}

void APartnerPlayerController::BindPlayerStateDelegates()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (BoundOutlierPlayerState == OutlierPlayerState)
	{
		return;
	}

	UnbindPlayerStateDelegates();
	if (!OutlierPlayerState)
	{
		return;
	}
	BoundOutlierPlayerState = OutlierPlayerState;
	BoundOutlierPlayerState->OnPlayerCharactersChanged.AddUObject(
		this,
		&APartnerPlayerController::HandlePlayerCharactersChanged
	);
	BoundOutlierPlayerState->OnAcquiredSuitChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleAcquiredSuitChanged
	);
}

void APartnerPlayerController::UnbindPlayerStateDelegates()
{
	if (!BoundOutlierPlayerState)
	{
		return;
	}

	BoundOutlierPlayerState->OnPlayerCharactersChanged.RemoveAll(this);
	BoundOutlierPlayerState->OnAcquiredSuitChanged.RemoveAll(this);
	BoundOutlierPlayerState = nullptr;
}

void APartnerPlayerController::HandlePlayerCharactersChanged(AOutlierPlayerState* ChangedPlayerState)
{
	BindShooterCharacterDelegatesFromPlayerState();
	RefreshPartnerSuitUI();
}

void APartnerPlayerController::HandleAcquiredSuitChanged(AOutlierPlayerState* ChangedPlayerState)
{
	if (ChangedPlayerState == GetPlayerState<AOutlierPlayerState>())
	{
		RefreshPartnerSuitUI();
	}
}

void APartnerPlayerController::BindShooterCharacterDelegatesFromPlayerState()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: PlayerState is null PC=%s"), *GetNameSafe(this));
		return;
	}

	AShooterCharacter* ShooterCharacter = OutlierPlayerState->GetShooterCharacter();
	if (!ShooterCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: ShooterCharacter is null PS=%s"), *GetNameSafe(OutlierPlayerState));
		return;
	}

	if (!GetLocalUISubsystem())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: UISubsystem is null PC=%s"), *GetNameSafe(this));
		return;
	}

	UnbindShooterCharacterDelegates();
	BoundShooterCharacter = ShooterCharacter;
	BoundShooterAbilitySystem = ShooterCharacter->GetOutlierAbilitySystemComponent();

	if (BoundShooterAbilitySystem)
	{
		HealthChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterHealthAttributeChanged);
		MaxHealthChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterHealthAttributeChanged);
		ShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetShieldAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterShieldAttributeChanged);
		MaxShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxShieldAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterShieldAttributeChanged);
		PartnerShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterPartnerShieldAttributeChanged);
		MaxPartnerShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).AddUObject(
				this, &APartnerPlayerController::HandleShooterPartnerShieldAttributeChanged);
	}

	RefreshShooterVitalityUI();
}

void APartnerPlayerController::UnbindShooterCharacterDelegates()
{
	if (!BoundShooterCharacter && !BoundShooterAbilitySystem)
	{
		return;
	}

	if (BoundShooterAbilitySystem)
	{
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetShieldAttribute()).Remove(ShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxShieldAttribute()).Remove(MaxShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).Remove(PartnerShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).Remove(MaxPartnerShieldChangedHandle);
	}
	BoundShooterAbilitySystem = nullptr;
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	ShieldChangedHandle.Reset();
	MaxShieldChangedHandle.Reset();
	PartnerShieldChangedHandle.Reset();
	MaxPartnerShieldChangedHandle.Reset();
	BoundShooterCharacter = nullptr;
}

void APartnerPlayerController::RefreshShooterVitalityUI()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	HandleShooterHealthChanged(BoundShooterCharacter->GetCurHealth(), BoundShooterCharacter->GetMaxHealth());
	HandleShooterShieldChanged(BoundShooterCharacter->GetCurShield(), BoundShooterCharacter->GetMaxShield());
	HandleShooterPartnerShieldChanged(
		BoundShooterCharacter->GetCurPartnerShield(),
		BoundShooterCharacter->GetMaxPartnerShield());
	HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
}

void APartnerPlayerController::HandleShooterHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterHealthChanged(BoundShooterCharacter->GetCurHealth(), BoundShooterCharacter->GetMaxHealth());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

void APartnerPlayerController::HandleShooterShieldAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterShieldChanged(BoundShooterCharacter->GetCurShield(), BoundShooterCharacter->GetMaxShield());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

void APartnerPlayerController::HandleShooterPartnerShieldAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterPartnerShieldChanged(
			BoundShooterCharacter->GetCurPartnerShield(),
			BoundShooterCharacter->GetMaxPartnerShield());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

ULocalPlayerUISubSystem* APartnerPlayerController::GetLocalUISubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
}

void APartnerPlayerController::HandleShooterHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_HealthChanged(CurrentHealth, MaxHealth);
	}
}

void APartnerPlayerController::HandleShooterShieldChanged(float CurrentShield, float MaxShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShieldChanged(CurrentShield, MaxShield);
	}
}

void APartnerPlayerController::HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_PartnerShieldChanged(CurrentPartnerShield, MaxPartnerShield);
	}
}

void APartnerPlayerController::HandleShooterConditionChanged(const FGameplayTag& ConditionTag)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShooterHPStateChanged(ConditionTag);
	}
}

void APartnerPlayerController::HandlePartnerHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_PartnerHealthChanged(CurrentHealth, MaxHealth);
	}
}
