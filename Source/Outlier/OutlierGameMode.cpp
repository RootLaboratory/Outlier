// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpoint.h"
#include "OutlierGameState.h"
#include "FrontendPlayerController.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Network/OutlierArenaPausePlayerState.h"
#include "Network/OutlierArenaProcessSubsystem.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "Save/OutlierSaveSubSystem.h"
#include "GameFramework/GameStateBase.h"
#include "Shooter/ShooterPlayerController.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "OutlierLobbyIdentitySubsystem.h"
#include "OutlierArenaSettings.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

AOutlierGameMode::AOutlierGameMode()
{

}

void AOutlierGameMode::InitGameState()
{
	Super::InitGameState();
	PauseArenaWorkerWorld();
}

void AOutlierGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ArenaWorkerPairSetupTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaWorkerPairSetupTickerHandle);
		ArenaWorkerPairSetupTickerHandle.Reset();
	}
	if (ArenaWorkerGameplayStartTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaWorkerGameplayStartTickerHandle);
		ArenaWorkerGameplayStartTickerHandle.Reset();
	}

	ClearArenaWorkerWorldPause();
	Super::EndPlay(EndPlayReason);
}

bool AOutlierGameMode::IsArenaWorkerProcess() const
{
	return FParse::Param(FCommandLine::Get(), TEXT("OutlierArenaWorker"));
}

bool AOutlierGameMode::UsesStaticArenaHandoff() const
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	return IsArenaWorkerProcess()
		&& Settings
		&& Settings->bUseStaticArenaHandoff;
}

void AOutlierGameMode::PauseArenaWorkerWorld()
{
	UWorld* World = GetWorld();
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!HasAuthority()
		|| !IsArenaWorkerProcess()
		|| bArenaWorkerGameplayStarted
		|| !World
		|| !Settings
		|| !Settings->IsArenaWorld(World)
		|| World->IsPaused())
	{
		return;
	}

	AOutlierArenaPausePlayerState* PauseOwner =
		World->SpawnActorDeferred<AOutlierArenaPausePlayerState>(
		AOutlierArenaPausePlayerState::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!PauseOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaWorker] Failed to create the pre-match pause owner"));
		return;
	}

	PauseOwner->SetFlags(RF_Transient);
	UGameplayStatics::FinishSpawningActor(PauseOwner, FTransform::Identity);

	ArenaWorkerPauseOwner = PauseOwner;
	World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
	UE_LOG(LogTemp, Display, TEXT("[ArenaWorker] Arena world paused until both clients are ready"));
}

void AOutlierGameMode::ClearArenaWorkerWorldPause()
{
	UWorld* World = GetWorld();
	if (World && World->GetWorldSettings()->GetPauserPlayerState() == ArenaWorkerPauseOwner)
	{
		World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	}

	if (ArenaWorkerPauseOwner)
	{
		ArenaWorkerPauseOwner->Destroy();
		ArenaWorkerPauseOwner = nullptr;
	}
}

void AOutlierGameMode::ScheduleArenaWorkerPairSetup()
{
	if (bArenaWorkerPairStartScheduled || bArenaWorkerPairStarted)
	{
		return;
	}

	bArenaWorkerPairStartScheduled = true;
	ArenaWorkerPairSetupTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &AOutlierGameMode::HandleArenaWorkerPairSetupTick));
}

bool AOutlierGameMode::HandleArenaWorkerPairSetupTick(float DeltaTime)
{
	(void)DeltaTime;
	ArenaWorkerPairSetupTickerHandle.Reset();
	TryStartArenaWorkerPair();
	return false;
}

void AOutlierGameMode::RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint)
{
	if (!Controller || !Checkpoint)
	{
		return;
	}

	AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>();

	if (!PS)
	{
		return;
	}

	FOutlierCheckpointData Data;
	Data.LevelName = FName(*GetWorld()->GetMapName());
	Data.CheckpointId = Checkpoint->GetCheckpointId();

	ApplyCheckpointToPair(PS, Data);
}

void AOutlierGameMode::RefreshPairLinks(AOutlierPlayerState* TriggeringPlayerState)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	AShooterCharacter* Shooter = ShooterPlayerState
		? ShooterPlayerState->GetShooterCharacter()
		: nullptr;

	APartnerCharacter* Partner = PartnerPlayerState
		? PartnerPlayerState->GetPartnerCharacter()
		: nullptr;

	if (!Shooter && PartnerPlayerState)
	{
		Shooter = PartnerPlayerState->GetShooterCharacter();
	}

	if (!Partner && ShooterPlayerState)
	{
		Partner = ShooterPlayerState->GetPartnerCharacter();
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, Shooter, Partner);
}

void AOutlierGameMode::ApplyCheckpointToPair(AOutlierPlayerState* TriggeringPlayerState, const FOutlierCheckpointData& Data)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	const int32 PairId = TriggeringPlayerState->GetPairId();

	if (PairId == INDEX_NONE || !GameState)
	{
		TriggeringPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(TriggeringPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}

		return;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PairPlayerState = Cast<AOutlierPlayerState>(RawPlayerState);
		if (!PairPlayerState || PairPlayerState->GetPairId() != PairId)
		{
			continue;
		}

		PairPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(PairPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}
	}
}

void AOutlierGameMode::HandlePlayerDeath(AShooterCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr)
	{
		if (!PS->GetShooterCharacter())
		{
			PS->SetShooterCharacter(Character);
		}

		PS->SetSuitDisabledByPartnerBoundary(false);
	}

	Character->DetachFromControllerPendingDestroy();
	
	RespawnPairAtCheckpoint(Controller);
}

void AOutlierGameMode::StartMatchedPair(AController* FirstController, AController* SecondController, int32 PairId, int32 ArenaId, EOutlierPlayerRole FirstRole, EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return;
	}

	if (AFrontendPlayerController* FrontendShooterPC = Cast<AFrontendPlayerController>(FirstController))
	{
		FrontendShooterPC->ClientPrepareForMatch();
	}

	if (AFrontendPlayerController* FrontendPartnerPC = Cast<AFrontendPlayerController>(SecondController))
	{
		FrontendPartnerPC->ClientPrepareForMatch();
	}

	AOutlierPlayerState* FirstPS = FirstController->GetPlayerState<AOutlierPlayerState>();
	AOutlierPlayerState* SecondPS = SecondController->GetPlayerState<AOutlierPlayerState>();

	if (!FirstPS || !SecondPS)
	{
		return;
	}

	FirstPS->SetPairId(PairId);
	FirstPS->SetArenaId(ArenaId);
	FirstPS->SetPlayerRole(FirstRole);
	FirstPS->ClearPendingLobbyState();

	SecondPS->SetPairId(PairId);
	SecondPS->SetArenaId(ArenaId);
	SecondPS->SetPlayerRole(SecondRole);
	SecondPS->ClearPendingLobbyState();

	AController* ShooterController =
		FirstRole == EOutlierPlayerRole::Shooter
		? FirstController
		: SecondController;

	AController* PartnerController =
		SecondRole == EOutlierPlayerRole::Partner
		? SecondController
		: FirstController;

	AOutlierPlayerState* ShooterPS =
		ShooterController->GetPlayerState<AOutlierPlayerState>();

	AOutlierPlayerState* PartnerPS =
		PartnerController->GetPlayerState<AOutlierPlayerState>();

	FTransform ShooterSpawn;
	FTransform PartnerSpawn;

	if (!ResolveArenaSpawnTransforms(ArenaId, ShooterSpawn, PartnerSpawn))
	{
		AActor* FallbackStart = FindPlayerStart(ShooterController);
		ShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
		PartnerSpawn = ShooterSpawn;
		PartnerSpawn.AddToTranslation(ShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms success ArenaId=%d ShooterSpawn=%s PartnerSpawn=%s"),
			ArenaId,
			*ShooterSpawn.ToHumanReadableString(),
			*PartnerSpawn.ToHumanReadableString());
	}

	UOutlierLobbyIdentitySubsystem* Identity =
		GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr;

	FGuid ShooterPlayerId;
	FGuid PartnerPlayerId;

	const bool bHasShooterPlayerId =
		Identity && Identity->TryGetPlayerId(ShooterController, ShooterPlayerId);

	const bool bHasPartnerPlayerId =
		Identity && Identity->TryGetPlayerId(PartnerController, PartnerPlayerId);

	AShooterCharacter* Shooter = GetWorld()->SpawnActor<AShooterCharacter>(
		ShooterClass,
		ShooterSpawn
	);

	APartnerCharacter* Partner = GetWorld()->SpawnActor<APartnerCharacter>(
		PartnerClass,
		PartnerSpawn
	);

	APlayerController* NewShooterPC = nullptr;
	APlayerController* NewPartnerPC = nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ShooterControllerClass)
	{
		NewShooterPC = GetWorld()->SpawnActor<APlayerController>(ShooterControllerClass, ShooterSpawn, SpawnParams);
	}
	if (PartnerControllerClass)
	{
		NewPartnerPC = GetWorld()->SpawnActor<APlayerController>(PartnerControllerClass, PartnerSpawn, SpawnParams);
	}

	if (ShooterController && NewShooterPC)
	{
		APlayerController* OldShooterPC =
			Cast<APlayerController>(ShooterController);

		if (OldShooterPC && NewShooterPC)
		{
			SwapPlayerControllers(OldShooterPC, NewShooterPC);

			if (bHasShooterPlayerId &&
				!Identity->RebindPlayer(ShooterPlayerId, NewShooterPC))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[LobbyIdentity] Shooter rebind failed: %s"),
					*ShooterPlayerId.ToString());
			}
		}
	}
	if (PartnerController && NewPartnerPC)
	{
		APlayerController* OldPartnerPC =
			Cast<APlayerController>(PartnerController);

		if(OldPartnerPC && NewPartnerPC)
		{
			SwapPlayerControllers(OldPartnerPC, NewPartnerPC);

			if (bHasPartnerPlayerId &&
				!Identity->RebindPlayer(PartnerPlayerId, NewPartnerPC))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[LobbyIdentity] Failed to rebind Partner PlayerId=%s"),
					*PartnerPlayerId.ToString());
			}
		}
	}

	AOutlierPlayerState* NewShooterPS = NewShooterPC ? NewShooterPC->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AOutlierPlayerState* NewPartnerPS = NewPartnerPC ? NewPartnerPC->GetPlayerState<AOutlierPlayerState>() : nullptr;

	if (NewShooterPS)
	{
		NewShooterPS->SetPairId(PairId);
		NewShooterPS->SetArenaId(ArenaId);
		NewShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
		NewShooterPS->ClearPendingLobbyState();
	}

	if (NewPartnerPS)
	{
		NewPartnerPS->SetPairId(PairId);
		NewPartnerPS->SetArenaId(ArenaId);
		NewPartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
		NewPartnerPS->ClearPendingLobbyState();
	}

	if (IsArenaWorkerProcess())
	{
		ArenaWorkerShooterController = NewShooterPC;
		ArenaWorkerPartnerController = NewPartnerPC;
	}

	RegisterSpawnedPair(NewShooterPS, NewPartnerPS, Shooter, Partner);

	PossessMatchedPawn(NewShooterPC, Shooter, ArenaId);
	PossessMatchedPawn(NewPartnerPC, Partner, ArenaId);

	if (NewShooterPC && NewPartnerPC && Shooter && Partner)
	{
		TryScheduleArenaWorkerAutoComplete();
	}

}

bool AOutlierGameMode::CompleteArenaMatch()
{
	if (!HasAuthority()
		|| !IsArenaWorkerProcess()
		|| !bArenaWorkerPairStarted
		|| !bArenaWorkerGameplayStarted
		|| bArenaWorkerMatchCompleting)
	{
		return false;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const FString LobbyAddress = Settings
		? Settings->LobbyAddress.TrimStartAndEnd()
		: FString();
	if (!Settings
		|| !Settings->bReturnToLobbyOnMatchEnd
		|| LobbyAddress.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReturn] Lobby return is not configured"));
		return false;
	}

	bArenaWorkerMatchCompleting = true;
	GetWorldTimerManager().ClearTimer(ArenaWorkerAutoCompleteTimerHandle);
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr)
	{
		ProcessSubsystem->NotifyWorkerReleasing(ArenaWorkerAdmission.MatchId);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReturn] Match completed. Returning players to %s"),
		*LobbyAddress);

	if (APlayerController* ShooterController = ArenaWorkerShooterController.Get())
	{
		ShooterController->ClientTravel(LobbyAddress, TRAVEL_Absolute);
	}
	if (APlayerController* PartnerController = ArenaWorkerPartnerController.Get())
	{
		PartnerController->ClientTravel(LobbyAddress, TRAVEL_Absolute);
	}

	const float ExitTimeout = FMath::Max(
		Settings->ArenaWorkerExitTimeoutSeconds,
		0.1f);
	GetWorldTimerManager().SetTimer(
		ArenaWorkerExitTimerHandle,
		this,
		&AOutlierGameMode::RequestArenaWorkerExit,
		ExitTimeout,
		false);
	return true;
}

void AOutlierGameMode::PossessMatchedPawn(
	APlayerController* PlayerController,
	APawn* Pawn,
	int32 ArenaId)
{
	if (!PlayerController || !Pawn)
	{
		return;
	}

	if (IsArenaWorkerProcess() || PlayerController->IsLocalController())
	{
		if (IsArenaWorkerProcess())
		{
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				FirstPersonController->ClientPrepareForArenaStart();
			}
		}
		PlayerController->Possess(Pawn);
		return;
	}

	PendingPossessions.Add(PlayerController, Pawn);
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(PlayerController))
	{
		FirstPersonController->ClientArenaLoad(ArenaId);
	}
}



void AOutlierGameMode::OnClientArenaReady(APlayerController* PC)
{
	if (IsArenaWorkerProcess())
	{
		if (!bArenaWorkerPairStarted
			|| bArenaWorkerGameplayStarted
			|| (PC != ArenaWorkerShooterController.Get()
				&& PC != ArenaWorkerPartnerController.Get()))
		{
			return;
		}

		ArenaWorkerReadyPlayers.Add(PC);
		if (ArenaWorkerReadyPlayers.Contains(ArenaWorkerShooterController)
			&& ArenaWorkerReadyPlayers.Contains(ArenaWorkerPartnerController))
		{
			ScheduleArenaWorkerGameplayStart();
		}
		return;
	}

	/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] OnClientArenaReady PC=%s PendingCount=%d"),
		*GetNameSafe(PC), PendingPossessions.Num());*/

	TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(PC);
	if (!PendingPawn || !(*PendingPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] No pending pawn for PC=%s"), *GetNameSafe(PC));
		PendingPossessions.Remove(PC);
		return;
	}

	APawn* Pawn = PendingPawn->Get();
	PendingPossessions.Remove(PC);

	//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] Possessing Pawn=%s"), *GetNameSafe(Pawn));
	PC->Possess(Pawn);
}

void AOutlierGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	if (!ErrorMessage.IsEmpty() || !IsArenaWorkerProcess())
	{
		return;
	}

	if (GetNumPlayers() >= 2)
	{
		ErrorMessage = TEXT("Arena worker already has two players");
		return;
	}

	if (!UsesStaticArenaHandoff())
	{
		return;
	}

	FOutlierArenaHandoffRequest Request;
	if (!OutlierArenaHandoff::TryParseOptions(Options, Request, ErrorMessage))
	{
		return;
	}

	if (const UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr;
		ProcessSubsystem && !ProcessSubsystem->CanWorkerAcceptMatch(Request.MatchId))
	{
		ErrorMessage = TEXT("Arena worker is reserved for another match");
		return;
	}

	ArenaWorkerAdmission.CanAccept(Request, ErrorMessage);
}

FString AOutlierGameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(
		NewPlayerController,
		UniqueId,
		Options,
		Portal);
	if (!ErrorMessage.IsEmpty() || !UsesStaticArenaHandoff())
	{
		return ErrorMessage;
	}

	FOutlierArenaHandoffRequest Request;
	if (!OutlierArenaHandoff::TryParseOptions(Options, Request, ErrorMessage)
		|| !ArenaWorkerAdmission.CanAccept(Request, ErrorMessage))
	{
		return ErrorMessage;
	}

	if (const UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr;
		ProcessSubsystem && !ProcessSubsystem->CanWorkerAcceptMatch(Request.MatchId))
	{
		return TEXT("Arena worker is reserved for another match");
	}

	UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr;
	AOutlierPlayerState* PlayerState = NewPlayerController
		? NewPlayerController->GetPlayerState<AOutlierPlayerState>()
		: nullptr;
	if (!Identity || !PlayerState
		|| !Identity->RebindPlayer(Request.PlayerId, NewPlayerController))
	{
		return TEXT("Failed to restore arena player identity");
	}

	PlayerState->SetPlayerRole(Request.Role);
	if (!ArenaWorkerAdmission.Commit(Request, ErrorMessage))
	{
		Identity->UnregisterPlayer(NewPlayerController);
		return ErrorMessage;
	}

	if (Request.Role == EOutlierPlayerRole::Shooter)
	{
		ArenaWorkerShooterController = NewPlayerController;
	}
	else
	{
		ArenaWorkerPartnerController = NewPlayerController;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ArenaWorker] Admitted Match=%s Player=%s Role=%s"),
		*Request.MatchId.ToString(),
		*Request.PlayerId.ToString(),
		Request.Role == EOutlierPlayerRole::Shooter
			? TEXT("Shooter")
			: TEXT("Partner"));
	return FString();
}

void AOutlierGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (IsArenaWorkerProcess())
	{
		// StartMatchedPair에서 Pawn 생성과 Possess를 처리하므로 기본 Spawn은 생략.
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AOutlierGameMode::Logout(AController* Exiting)
{
	ArenaWorkerPlayers.Remove(Cast<APlayerController>(Exiting));

	if (UsesStaticArenaHandoff() && !bArenaWorkerPairStarted && Exiting)
	{
		if (AOutlierPlayerState* PlayerState =
			Exiting->GetPlayerState<AOutlierPlayerState>())
		{
			ArenaWorkerAdmission.Release(PlayerState->GetTemporaryPlayerId());
		}
	}

	if (ArenaWorkerShooterController.Get() == Exiting)
	{
		ArenaWorkerShooterController.Reset();
	}
	if (ArenaWorkerPartnerController.Get() == Exiting)
	{
		ArenaWorkerPartnerController.Reset();
	}

	if (Exiting)
	{
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(Exiting->GetPawn()))
		{
			ShooterCharacter->CleanupOwnedWeapons();
		}

		if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(Exiting))
		{
			if (AEnemyBase* EnemyPawn = Cast<AEnemyBase>(Exiting->GetPawn()))
			{
				// 캐시를 먼저 비워야 함 — 아래 Destroy()가 트리거하는 PawnPendingDestroy() 안전장치가
				// 이미 지워질 캐시된 Partner를 복원하려고 시도하는 걸 막기 위함.
				// AI에게 돌려주지 않고 바로 지우는 건, 일반 Shooter/Partner 로그아웃과 동일하게
				// 빙의 중이던 Pawn과 원래 Pawn이 둘 다 사라지는 게 자연스럽다는 판단.
				APartnerCharacter* CachedPartner = PartnerController->ExtractCachedPartnerCharacterForLogout();

				EnemyPawn->ClearPossessedPlayerState();
				EnemyPawn->Destroy();

				if (CachedPartner)
				{
					CachedPartner->Destroy();
				}
			}
		}
	}

	if (Cast<AFrontendPlayerController>(Exiting))
	{
		if (UOutlierMatchmakingSubsystem* Matchmaking = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierMatchmakingSubsystem>()
			: nullptr)
		{
			Matchmaking->Cancel(Exiting);
		}
	}

	if (UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr)
	{
		Identity->UnregisterPlayer(Exiting);
	}

	Super::Logout(Exiting);

	if (bArenaWorkerMatchCompleting
		&& !ArenaWorkerShooterController.IsValid()
		&& !ArenaWorkerPartnerController.IsValid())
	{
		RequestArenaWorkerExit();
	}
}


void AOutlierGameMode::RespawnPairAtCheckpoint(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPlayerState = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	if (!ShooterPlayerState)
	{
		ShooterPlayerState = TriggeringPlayerState;
	}

	const int32 ArenaId = ShooterPlayerState->GetArenaId();

	FTransform SpawnTransform;
	FTransform PartnerSpawnTransform;

	if (ResolveCheckpointTransform(GetControllerFromPlayerState(ShooterPlayerState), ArenaId, SpawnTransform))
	{
		PartnerSpawnTransform = SpawnTransform;
		PartnerSpawnTransform.AddToTranslation(
			SpawnTransform.GetRotation().GetRightVector() * 150.0f
		);
	}
	else if (ResolveArenaSpawnTransforms(ArenaId, SpawnTransform, PartnerSpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Respawn] Checkpoint missing. Fallback to arena start. ArenaId=%d Spawn=%s PartnerSpawn=%s"),
			ArenaId,
			*SpawnTransform.ToHumanReadableString(),
			*PartnerSpawnTransform.ToHumanReadableString());
	}
	else
	{
		AActor* PlayerStart = FindPlayerStart(Controller);
		SpawnTransform = PlayerStart
			? PlayerStart->GetActorTransform()
			: FTransform::Identity;

		PartnerSpawnTransform.AddToTranslation(
			SpawnTransform.GetRotation().GetRightVector() * 150.0f
		);

		UE_LOG(LogTemp, Warning,
			TEXT("[Respawn] Arena fallback failed. Fallback to FindPlayerStart. ArenaId=%d PlayerStart=%s Spawn=%s"),
			ArenaId,
			*GetNameSafe(PlayerStart),
			*SpawnTransform.ToHumanReadableString());
	}


	AShooterCharacter* OldShooter = ShooterPlayerState->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPlayerState->GetPartnerCharacter();

	if (!OldPartner && PartnerPlayerState)
	{
		OldPartner = PartnerPlayerState->GetPartnerCharacter();
	}

	if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(GetControllerFromPlayerState(PartnerPlayerState)))
	{
		if (Cast<AEnemyBase>(PartnerController->GetPawn()))
		{
			PartnerController->ReleaseEnemyPossession();
		}
	}

	ShooterPlayerState->SetShooterCharacter(nullptr);
	ShooterPlayerState->SetPartnerCharacter(nullptr);
	ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(nullptr);
		PartnerPlayerState->SetPartnerCharacter(nullptr);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		EnemyRoomSubsystem->NotifyTargetActorRemoved(OldShooter);
		EnemyRoomSubsystem->NotifyTargetActorRemoved(OldPartner);
	}

	if (OldShooter)
	{
		OldShooter->CleanupOwnedWeapons();
		OldShooter->Destroy();
	}

	if (OldPartner)
	{
		OldPartner->Destroy();
	}

	AShooterCharacter* NewShooter = nullptr;
	if (ShooterClass)
	{
		NewShooter = GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, SpawnTransform);
	}
	else if (DefaultPawnClass)
	{
		NewShooter = Cast<AShooterCharacter>(
			GetWorld()->SpawnActor<APawn>(DefaultPawnClass, SpawnTransform)
		);
	}

	APartnerCharacter* NewPartner = PartnerClass
		? GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, PartnerSpawnTransform)
		: nullptr;

	if (AController* ShooterController = GetControllerFromPlayerState(ShooterPlayerState))
	{
		if (NewShooter)
		{
			ShooterController->Possess(NewShooter);
		}
	}

	if (AController* PartnerController = GetControllerFromPlayerState(PartnerPlayerState))
	{
		if (NewPartner)
		{
			PartnerController->Possess(NewPartner);
		}
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, NewShooter, NewPartner);
}

void AOutlierGameMode::DebugReloadArena(AController* Requester)
{
	if (!Requester)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPS = Requester->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPS)
	{
		return;
	}

	const int32 PairId = TriggeringPS->GetPairId();

	AOutlierPlayerState* ShooterPS = TriggeringPS->IsShooterPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Shooter);
	AOutlierPlayerState* PartnerPS = TriggeringPS->IsPartnerPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Partner);
	if (!ShooterPS)
	{
		ShooterPS = TriggeringPS;
	}

	const int32 ArenaId = ShooterPS->GetArenaId();
	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] DebugReloadArena PairId=%d ArenaId=%d ShooterPS=%s PartnerPS=%s"),
		PairId, ArenaId, *GetNameSafe(ShooterPS), *GetNameSafe(PartnerPS));
	if (ArenaId == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] Bail: ArenaId==INDEX_NONE"));
		return;
	}

	//시작은 save 상관없이 start
	FTransform ShooterSpawn;
	FTransform PartnerSpawn;
	if (!ResolveArenaSpawnTransforms(ArenaId, ShooterSpawn, PartnerSpawn))
	{
		AActor* FallbackStart = FindPlayerStart(Requester);
		ShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
		PartnerSpawn = ShooterSpawn;
		PartnerSpawn.AddToTranslation(ShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	// 2) 기존 폰 정리 (RespawnPairAtCheckpoint와 동일). 파트너가 적 빙의 중이면 먼저 해제.
	AShooterCharacter* OldShooter = ShooterPS->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPS->GetPartnerCharacter();
	if (!OldPartner && PartnerPS)
	{
		OldPartner = PartnerPS->GetPartnerCharacter();
	}

	if (APartnerPlayerController* PartnerPC = Cast<APartnerPlayerController>(GetControllerFromPlayerState(PartnerPS)))
	{
		if (Cast<AEnemyBase>(PartnerPC->GetPawn()))
		{
			PartnerPC->ReleaseEnemyPossession();
		}
	}

	ShooterPS->SetShooterCharacter(nullptr);
	ShooterPS->SetPartnerCharacter(nullptr);
	ShooterPS->SetSuitDisabledByPartnerBoundary(false);
	if (PartnerPS)
	{
		PartnerPS->SetShooterCharacter(nullptr);
		PartnerPS->SetPartnerCharacter(nullptr);
		PartnerPS->SetSuitDisabledByPartnerBoundary(false);
	}

	if (OldShooter)
	{
		OldShooter->CleanupOwnedWeapons();
		OldShooter->Destroy();
	}
	if (OldPartner)
	{
		OldPartner->Destroy();
	}

	AShooterCharacter* NewShooter = ShooterClass
		? GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, ShooterSpawn)
		: nullptr;
	APartnerCharacter* NewPartner = PartnerClass
		? GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, PartnerSpawn)
		: nullptr;

	RegisterSpawnedPair(ShooterPS, PartnerPS, NewShooter, NewPartner);

	// 4) possess 배선 — 지오메트리 준비 후로 지연.
	//    remote → 클라 재스트리밍 후 OnClientArenaReady에서 possess
	//    local  → 서버 자기 재스트리밍(OnArenaShown) 후 possess (아래 5)
	AController* ShooterController = GetControllerFromPlayerState(ShooterPS);
	AController* PartnerController = GetControllerFromPlayerState(PartnerPS);

	if (APlayerController* PC = Cast<APlayerController>(ShooterController))
	{
		if (PC->IsLocalController())
		{
			PendingLocalPossessions.Add(PC, NewShooter);
		}
		else
		{
			PendingPossessions.Add(PC, NewShooter);
			if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(PC))
			{
				FPC->ClientArenaReload(ArenaId);
			}
		}
	}

	if (APlayerController* PC = Cast<APlayerController>(PartnerController))
	{
		if (PC->IsLocalController())
		{
			PendingLocalPossessions.Add(PC, NewPartner);
		}
		else
		{
			PendingPossessions.Add(PC, NewPartner);
			if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(PC))
			{
				FPC->ClientArenaReload(ArenaId);
			}
		}
	}

	// 5) 로컬 possess 대기 바인딩 + 서버측 리로드 시작
	UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	if (!ArenaPool)
	{
		return;
	}

	if (PendingLocalPossessions.Num() > 0)
	{
		ReloadingArenaId = ArenaId;
		if (!ArenaShownHandle.IsValid())
		{
			ArenaShownHandle = ArenaPool->OnArenaShown.AddUObject(this, &AOutlierGameMode::HandleServerArenaReloaded);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Calling ReloadArena ArenaId=%d LocalPending=%d RemotePending=%d"),
		ArenaId, PendingLocalPossessions.Num(), PendingPossessions.Num());
	ArenaPool->ReloadArena(ArenaId);
}

void AOutlierGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (Cast<AFrontendPlayerController>(NewPlayer))
	{
		if (UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
			: nullptr)
		{
			Identity->RegisterPlayer(NewPlayer);
		}
	}

	if (!IsArenaWorkerProcess() || bArenaWorkerPairStarted)
	{
		return;
	}

	if (UsesStaticArenaHandoff())
	{
		if (ArenaWorkerAdmission.IsReady()
			&& ArenaWorkerShooterController.IsValid()
			&& ArenaWorkerPartnerController.IsValid()
			&& !bArenaWorkerPairStartScheduled)
		{
			// 두 번째 PostLogin이 끝난 뒤 Controller 교체를 시작한다.
			ScheduleArenaWorkerPairSetup();
		}
		return;
	}

	ArenaWorkerPlayers.AddUnique(NewPlayer);
	ArenaWorkerPlayers.RemoveAll([](const TWeakObjectPtr<APlayerController>& Player)
	{
		return !Player.IsValid();
	});

	if (ArenaWorkerPlayers.Num() == 2
		&& !bArenaWorkerPairStartScheduled)
	{
		// 두 번째 PostLogin이 완전히 끝난 뒤 PlayerController 교체를 시작하도록 다음 틱까지 지연.
		ScheduleArenaWorkerPairSetup();
	}
}

void AOutlierGameMode::TryStartArenaWorkerPair()
{
	bArenaWorkerPairStartScheduled = false;

	if (UsesStaticArenaHandoff())
	{
		APlayerController* ShooterController = ArenaWorkerShooterController.Get();
		APlayerController* PartnerController = ArenaWorkerPartnerController.Get();
		if (bArenaWorkerPairStarted
			|| !ArenaWorkerAdmission.IsReady()
			|| !ShooterController
			|| !PartnerController)
		{
			return;
		}

		bArenaWorkerPairStarted = true;
		ArenaWorkerAdmission.bPairStarted = true;

		UE_LOG(LogTemp, Display,
			TEXT("[ArenaWorker] Preparing assigned pair Match=%s ArenaId=0"),
			*ArenaWorkerAdmission.MatchId.ToString());
		StartMatchedPair(
			ShooterController,
			PartnerController,
			/*PairId=*/0,
			/*ArenaId=*/0,
			EOutlierPlayerRole::Shooter,
			EOutlierPlayerRole::Partner);
		return;
	}

	ArenaWorkerPlayers.RemoveAll([](const TWeakObjectPtr<APlayerController>& Player)
	{
		return !Player.IsValid();
	});

	if (bArenaWorkerPairStarted || ArenaWorkerPlayers.Num() != 2)
	{
		return;
	}

	APlayerController* ShooterController = ArenaWorkerPlayers[0].Get();
	APlayerController* PartnerController = ArenaWorkerPlayers[1].Get();
	if (!ShooterController || !PartnerController)
	{
		return;
	}

	bArenaWorkerPairStarted = true;
	ArenaWorkerPlayers.Reset();

	UE_LOG(LogTemp, Display,
		TEXT("[ArenaWorker] Starting direct-connect pair with ArenaId=0"));
	StartMatchedPair(
		ShooterController,
		PartnerController,
		/*PairId=*/0,
		/*ArenaId=*/0,
		EOutlierPlayerRole::Shooter,
		EOutlierPlayerRole::Partner);
}

void AOutlierGameMode::ScheduleArenaWorkerGameplayStart()
{
	if (bArenaWorkerGameplayStartScheduled || bArenaWorkerGameplayStarted)
	{
		return;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const float StartDelay = Settings
		? FMath::Max(Settings->ArenaMatchStartDelaySeconds, 0.0f)
		: 1.0f;
	bArenaWorkerGameplayStartScheduled = true;
	ArenaWorkerGameplayStartTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &AOutlierGameMode::HandleArenaWorkerGameplayStartTick),
		StartDelay);
}

bool AOutlierGameMode::HandleArenaWorkerGameplayStartTick(float DeltaTime)
{
	(void)DeltaTime;
	ArenaWorkerGameplayStartTickerHandle.Reset();
	bArenaWorkerGameplayStartScheduled = false;
	StartArenaWorkerGameplay();
	return false;
}

void AOutlierGameMode::StartArenaWorkerGameplay()
{
	APlayerController* ShooterController = ArenaWorkerShooterController.Get();
	APlayerController* PartnerController = ArenaWorkerPartnerController.Get();
	if (bArenaWorkerGameplayStarted
		|| !bArenaWorkerPairStarted
		|| !ShooterController
		|| !PartnerController
		|| !ArenaWorkerReadyPlayers.Contains(ShooterController)
		|| !ArenaWorkerReadyPlayers.Contains(PartnerController))
	{
		return;
	}

	bArenaWorkerGameplayStarted = true;
	ClearArenaWorkerWorldPause();

	if (UsesStaticArenaHandoff())
	{
		if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
			: nullptr)
		{
			ProcessSubsystem->NotifyWorkerInMatch(ArenaWorkerAdmission.MatchId);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[ArenaWorker] Both clients are ready. Gameplay started"));
}

void AOutlierGameMode::RequestArenaWorkerExit()
{
	if (bArenaWorkerExitRequested
		|| !bArenaWorkerMatchCompleting
		|| !IsArenaWorkerProcess()
		|| !IsRunningDedicatedServer())
	{
		return;
	}

	bArenaWorkerExitRequested = true;
	GetWorldTimerManager().ClearTimer(ArenaWorkerExitTimerHandle);
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReturn] Arena Worker exit requested after Match completion"));
	RequestEngineExit(TEXT("Outlier Arena Worker match completed"));
}

void AOutlierGameMode::TryScheduleArenaWorkerAutoComplete()
{
	if (!IsArenaWorkerProcess() || bArenaWorkerMatchCompleting)
	{
		return;
	}

	float AutoCompleteSeconds = 0.0f;
	// N7 멀티 프로세스 Smoke에서만 명시적으로 활성화하는 임시 종료 경로다.
	if (!FParse::Value(
		FCommandLine::Get(),
		TEXT("OutlierArenaAutoCompleteSeconds="),
		AutoCompleteSeconds)
		|| AutoCompleteSeconds <= 0.0f)
	{
		return;
	}

	AutoCompleteSeconds = FMath::Max(AutoCompleteSeconds, 0.1f);
	UE_LOG(LogTemp, Display,
		TEXT("[NetworkMVP] Arena Match auto-complete scheduled in %.1f seconds"),
		AutoCompleteSeconds);
	GetWorldTimerManager().SetTimer(
		ArenaWorkerAutoCompleteTimerHandle,
		this,
		&AOutlierGameMode::HandleArenaWorkerAutoComplete,
		AutoCompleteSeconds,
		false);
}

void AOutlierGameMode::HandleArenaWorkerAutoComplete()
{
	if (!CompleteArenaMatch())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[NetworkMVP] Arena Match auto-complete request was rejected"));
	}
}

void AOutlierGameMode::HandleServerArenaReloaded(int32 ReloadedArenaId)
{
	if (ReloadedArenaId != ReloadingArenaId)
	{
		return;
	}

	for (auto It = PendingLocalPossessions.CreateIterator(); It; ++It)
	{
		APlayerController* PC = It->Key.Get();
		APawn* Pawn = It->Value.Get();
		if (PC && Pawn)
		{
			PC->Possess(Pawn);
		}
	}
	PendingLocalPossessions.Empty();

	if (UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr)
	{
		ArenaPool->OnArenaShown.Remove(ArenaShownHandle);
	}
	ArenaShownHandle.Reset();
	ReloadingArenaId = INDEX_NONE;
}

bool AOutlierGameMode::ResolveCheckpointTransform(AController* Controller, int32 ArenaId, FTransform& OutTransform) const
{
	const AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (!PS || !PS->GetCheckpointData().IsValid())
	{
		return false;
	}

	const FOutlierCheckpointData& Data = PS->GetCheckpointData();

	ULevel* ArenaLevel = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
		}
	}

	if (!ArenaLevel)
	{
		return false;
	}

	TArray<AActor*> Checkpoints;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(),
		AOutlierCheckpoint::StaticClass(),
		Checkpoints
	);

	for (AActor* Actor : Checkpoints)
	{
		const AOutlierCheckpoint* Checkpoint = Cast<AOutlierCheckpoint>(Actor);
		if (!Checkpoint)
		{
			continue;
		}

		if (Checkpoint->GetLevel() != ArenaLevel)
		{
			continue;
		}

		if (Checkpoint->GetCheckpointId() == Data.CheckpointId)
		{
			OutTransform = Checkpoint->GetSpawnTransform();
			return true;
		}
	}

	return false;
}

FString AOutlierGameMode::GetPlayerSaveId(AController* Controller) const
{
	if (!Controller)
	{
		return TEXT("InvalidPlayer");
	}

	if (APlayerState* PS = Controller->PlayerState)
	{
		const FString PlayerName = PS->GetPlayerName();
		if (!PlayerName.IsEmpty())
		{
			return PlayerName;
		}

		return FString::Printf(TEXT("Player_%d"), PS->GetPlayerId());
	}

	return Controller->GetName();
}

AOutlierPlayerState* AOutlierGameMode::FindPairPlayerState(int32 PairId, EOutlierPlayerRole PlayerRole) const
{
	if (PairId == INDEX_NONE || !GameState)
	{
		return nullptr;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PS = Cast<AOutlierPlayerState>(RawPlayerState);
		if (PS && PS->GetPairId() == PairId && PS->GetPlayerRole() == PlayerRole)
		{
			return PS;
		}
	}

	return nullptr;
}

AController* AOutlierGameMode::GetControllerFromPlayerState(AOutlierPlayerState* PlayerState) const
{
	return PlayerState ? Cast<AController>(PlayerState->GetOwner()) : nullptr;
}

void AOutlierGameMode::RegisterSpawnedPair(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	AShooterCharacter* Shooter,
	APartnerCharacter* Partner)
{
	if (ShooterPlayerState)
	{
		ShooterPlayerState->SetShooterCharacter(Shooter);
		ShooterPlayerState->SetPartnerCharacter(Partner);
		ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(Shooter);
		PartnerPlayerState->SetPartnerCharacter(Partner);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}
}

bool AOutlierGameMode::ResolveArenaSpawnTransforms(int32 ArenaId, FTransform& OutShooterSpawn, FTransform& OutPartnerSpawn) const
{
	const UWorld* World = GetWorld();
	ULevel* ArenaLevel = nullptr;
	if (World)
	{
		if (const UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
		}
	}

	if (!ArenaLevel)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ArenaLevel ArenaId=%d"),
			ArenaId);
		return false;
	}

	TArray<AActor*> PlayerStartActors;
	UGameplayStatics::GetAllActorsOfClass(
		World,
		APlayerStart::StaticClass(),
		PlayerStartActors
	);

	APlayerStart* ShooterStart = nullptr;
	APlayerStart* PartnerStart = nullptr;
	TArray<APlayerStart*> ArenaStarts;

	for (AActor* Actor : PlayerStartActors)
	{
		APlayerStart* PlayerStart = Cast<APlayerStart>(Actor);
		if (!PlayerStart)
		{
			continue;
		}

		const bool bSameLevel = PlayerStart->GetLevel() == ArenaLevel;
	
		if (!bSameLevel)
		{
			continue;
		}

		ArenaStarts.Add(PlayerStart);

		const FName StartTag = PlayerStart->PlayerStartTag;
		if (!ShooterStart && (StartTag == TEXT("Shooter") || StartTag == TEXT("Player1") || StartTag == TEXT("1P")))
		{
			ShooterStart = PlayerStart;
		}
		else if (!PartnerStart && (StartTag == TEXT("Partner") || StartTag == TEXT("Player2") || StartTag == TEXT("2P")))
		{
			PartnerStart = PlayerStart;
		}
	}

	if (!ShooterStart && ArenaStarts.Num() > 0)
	{
		ShooterStart = ArenaStarts[0];
	}

	if (!PartnerStart && ArenaStarts.Num() > 1)
	{
		PartnerStart = ArenaStarts[1];
	}

	if (!ShooterStart)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ShooterStart ArenaId=%d ArenaStartCount=%d"),
			ArenaId,
			ArenaStarts.Num());
		return false;
	}

	OutShooterSpawn = ShooterStart->GetActorTransform();
	if (PartnerStart)
	{
		OutPartnerSpawn = PartnerStart->GetActorTransform();
	}
	else
	{
		OutPartnerSpawn = OutShooterSpawn;
		OutPartnerSpawn.AddToTranslation(OutShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	return true;

}
