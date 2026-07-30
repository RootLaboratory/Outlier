// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerPlayerController.h"
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
#include "TimerManager.h"

APartnerPlayerController::APartnerPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Partner;
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void APartnerPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void APartnerPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	}

	UnbindShooterCharacterDelegates();
	UnbindPlayerStateDelegates();
	OnPartnerPossessionStateChanged.Clear();
	Super::EndPlay(EndPlayReason);
}

void APartnerPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindPlayerStateDelegates();
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

	if (!HasAuthority()
		|| EnemyTarget != ExpectedTarget
		|| !IsValid(EnemyTarget)
		|| !IsValid(PartnerCharacter)
		|| GetPawn() != PartnerCharacter
		|| EnemyTarget->IsEnemyPossessed())
	{
		CancelPendingEnemyPossessionTransition();
		return;
	}

	CachePartnerCharacterForEnemyPossession(PartnerCharacter);
	Possess(EnemyTarget);

	if (GetPawn() != EnemyTarget)
	{
		CachedPartnerCharacter.Reset();
		CancelPendingEnemyPossessionTransition();
		return;
	}

	PendingEnemyPossessionTarget.Reset();
	PendingEnemyPossessionSource.Reset();
	UnbindPossessionTargetEndPlay(EnemyTarget);
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
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
				LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
			{
				PPSubsystem->CancelHackPossessionTransition();
			}
		}
		break;

	case EPartnerPossessionState::EnemyPossessed:
		SetHackTransitionInputBlocked(false);
		break;
	}

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

	AEnemyBase* EnemyPawn = Cast<AEnemyBase>(GetPawn());
	if (!EnemyPawn)
	{
		return;
	}

	if (!CachedPartnerCharacter.IsValid())
	{
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

void APartnerPlayerController::ServerReleaseEnemyPossession_Implementation()
{
	ReleaseEnemyPossession();
}

APartnerCharacter* APartnerPlayerController::ExtractCachedPartnerCharacterForLogout()
{
	APartnerCharacter* Result = CachedPartnerCharacter.Get();
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
		CachedPartnerCharacter.Reset();
		return;
	}

	PartnerCharacter->SetEnemyPossessionProtection(false);
	Possess(PartnerCharacter);
	CachedPartnerCharacter.Reset();
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
	}
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
		PPSubsystem->OnHackTransitionCovered.AddUObject(
			this,
			&APartnerPlayerController::NotifyHackTransitionCovered);
		PPSubsystem->OnHackTransitionFinished.AddUObject(
			this,
			&APartnerPlayerController::HandleHackPossessionTransitionFinished);
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
	BindPostProcessSubSystem();
	BindPlayerStateDelegates();
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

void APartnerPlayerController::BindPlayerStateDelegates()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState || BoundOutlierPlayerState == OutlierPlayerState)
	{
		return;
	}

	UnbindPlayerStateDelegates();
	BoundOutlierPlayerState = OutlierPlayerState;
	BoundOutlierPlayerState->OnPlayerCharactersChanged.AddUObject(
		this,
		&APartnerPlayerController::HandlePlayerCharactersChanged
	);

}

void APartnerPlayerController::UnbindPlayerStateDelegates()
{
	if (!BoundOutlierPlayerState)
	{
		return;
	}

	BoundOutlierPlayerState->OnPlayerCharactersChanged.RemoveAll(this);
	BoundOutlierPlayerState = nullptr;
}

void APartnerPlayerController::HandlePlayerCharactersChanged(AOutlierPlayerState* ChangedPlayerState)
{
	BindShooterCharacterDelegatesFromPlayerState();
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

	ShooterCharacter->OnShooterHealthChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterHealthChanged
	);

	ShooterCharacter->OnShooterShieldChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterShieldChanged
	);

	ShooterCharacter->OnShooterPartnerShieldChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterPartnerShieldChanged
	);

	ShooterCharacter->OnShooterConditionChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterConditionChanged
	);

	ShooterCharacter->BroadcastCurrentUIState();
}

void APartnerPlayerController::UnbindShooterCharacterDelegates()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter->OnShooterHealthChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterPartnerShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterConditionChanged.RemoveAll(this);
	BoundShooterCharacter = nullptr;
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
