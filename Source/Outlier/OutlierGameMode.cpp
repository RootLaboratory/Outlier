// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Outlier.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/WeaponBase.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpoint.h"
#include "Save/OutlierCheckpointSnapshot.h"
#include "Save/PresetPlayerStart.h"
#include "Upgrade/PresetNodeProvideRow.h"
#include "Engine/DataTable.h"
#include "OutlierGameState.h"
#include "FrontendPlayerController.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Network/OutlierArenaPausePlayerState.h"
#include "Network/OutlierArenaProcessSubsystem.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/NetDriver.h"
#include "Network/OutlierMatchmakingSubsystem.h"
#include "Save/OutlierSaveSubSystem.h"
#include "GameFramework/GameStateBase.h"
#include "Shooter/ShooterPlayerController.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Room/RoomCombatSubsystem.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "OutlierLobbyIdentitySubsystem.h"
#include "OutlierArenaSettings.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Streaming/ServerStreamingLevelsVisibility.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

AOutlierGameMode::AOutlierGameMode()
{

}

bool AOutlierGameMode::CanControllerRequestCheckpointRestart(
	const APlayerController* Controller) const
{
	return Controller
		&& !bArenaReloadInProgress
		&& CheckpointRestartVote.GetState() == EOutlierCheckpointRestartVoteState::Idle
		&& OutlierCheckpointRestartVote::CanRequest(
			GetNetMode(),
			Controller->IsLocalController());
}

bool AOutlierGameMode::RequestCheckpointRestart(
	AFirstPersonPlayerController* Requester)
{
	if (!HasAuthority() || !CanControllerRequestCheckpointRestart(Requester))
	{
		return false;
	}

	UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	FOutlierCheckpointSnapshot RestoreSnapshot;
	if (!SaveSubsystem || !SaveSubsystem->GetRestoreSnapshot(RestoreSnapshot))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint.RestartVote] Request rejected: restore snapshot is unavailable Requester=%s"),
			*GetNameSafe(Requester));
		return false;
	}

	CheckpointRestartVoteLayerOwner = Requester->GetPawn();
	if (GetNetMode() == NM_Standalone)
	{
		// Standalone은 응답할 상대가 없으므로 요청 자체를 승인으로 취급한다.
		Requester->SetCheckpointRestartVoteViewFromServer(
			EOutlierCheckpointRestartVoteView::None);
		Requester->CloseCheckpointRestartVoteUIFromServer(
			CheckpointRestartVoteLayerOwner.Get());
		LastCheckpointRestartVoteResult = EOutlierCheckpointRestartVoteState::Restarting;
		bCheckpointRestartInProgress = true;
		if (StartCheckpointRestart(Requester))
		{
			return true;
		}

		bCheckpointRestartInProgress = false;
		LastCheckpointRestartVoteResult = EOutlierCheckpointRestartVoteState::Rejected;
		CheckpointRestartVoteLayerOwner.Reset();
		UGameplayStatics::SetGamePaused(this, false);
		return false;
	}

	AOutlierPlayerState* RequesterPlayerState =
		Requester->GetPlayerState<AOutlierPlayerState>();
	if (!RequesterPlayerState)
	{
		CheckpointRestartVoteLayerOwner.Reset();
		return false;
	}

	const EOutlierPlayerRole ResponderRole = RequesterPlayerState->IsShooterPlayer()
		? EOutlierPlayerRole::Partner
		: RequesterPlayerState->IsPartnerPlayer()
			? EOutlierPlayerRole::Shooter
			: EOutlierPlayerRole::None;
	AOutlierPlayerState* ResponderPlayerState = ResponderRole != EOutlierPlayerRole::None
		? FindPairPlayerState(RequesterPlayerState->GetPairId(), ResponderRole)
		: nullptr;
	AFirstPersonPlayerController* Responder = Cast<AFirstPersonPlayerController>(
		GetControllerFromPlayerState(ResponderPlayerState));
	if (!CheckpointRestartVote.Begin(Requester, Responder))
	{
		CheckpointRestartVoteLayerOwner.Reset();
		return false;
	}

	LastCheckpointRestartVoteResult = EOutlierCheckpointRestartVoteState::VotePending;
	Requester->SetCheckpointRestartVoteViewFromServer(
		EOutlierCheckpointRestartVoteView::RequesterWaiting);
	Responder->SetCheckpointRestartVoteViewFromServer(
		EOutlierCheckpointRestartVoteView::ResponderPrompt);
	return true;
}

bool AOutlierGameMode::RespondCheckpointRestart(
	AFirstPersonPlayerController* Responder,
	bool bApprove)
{
	if (!HasAuthority() || !CheckpointRestartVote.Respond(Responder, bApprove))
	{
		return false;
	}

	FinishCheckpointRestartVote(CheckpointRestartVote.GetState());
	return true;
}

bool AOutlierGameMode::CancelCheckpointRestart(
	AFirstPersonPlayerController* Requester)
{
	if (!HasAuthority() || !CheckpointRestartVote.Cancel(Requester))
	{
		return false;
	}

	FinishCheckpointRestartVote(EOutlierCheckpointRestartVoteState::Rejected);
	return true;
}

bool AOutlierGameMode::HandleCheckpointRestartEscape(
	AFirstPersonPlayerController* Controller)
{
	if (!CheckpointRestartVote.Contains(Controller))
	{
		return false;
	}

	if (CheckpointRestartVote.GetRequester() == Controller)
	{
		CancelCheckpointRestart(Controller);
	}
	else
	{
		RespondCheckpointRestart(Controller, false);
	}
	return true;
}

void AOutlierGameMode::FinishCheckpointRestartVote(
	EOutlierCheckpointRestartVoteState Result)
{
	AFirstPersonPlayerController* Requester = Cast<AFirstPersonPlayerController>(
		CheckpointRestartVote.GetRequester());
	AFirstPersonPlayerController* Responder = Cast<AFirstPersonPlayerController>(
		CheckpointRestartVote.GetResponder());
	UObject* LayerOwner = CheckpointRestartVoteLayerOwner.Get();

	if (Requester)
	{
		Requester->SetCheckpointRestartVoteViewFromServer(
			EOutlierCheckpointRestartVoteView::None);
		Requester->CloseCheckpointRestartVoteUIFromServer(LayerOwner);
	}
	if (Responder)
	{
		Responder->SetCheckpointRestartVoteViewFromServer(
			EOutlierCheckpointRestartVoteView::None);
		Responder->CloseCheckpointRestartVoteUIFromServer(LayerOwner);
	}

	if (Result == EOutlierCheckpointRestartVoteState::Approved)
	{
		if (CheckpointRestartVote.BeginRestart())
		{
			LastCheckpointRestartVoteResult = EOutlierCheckpointRestartVoteState::Restarting;
			bCheckpointRestartInProgress = true;
			if (StartCheckpointRestart(Requester))
			{
				UE_LOG(LogTemp, Log,
					TEXT("[Checkpoint.RestartVote] Approved; checkpoint reload started"));
				return;
			}
		}

		// 파괴적인 리로드를 시작하기 전 검증 실패는 기존 Pawn을 유지한 채 투표만 닫는다.
		bCheckpointRestartInProgress = false;
		Result = EOutlierCheckpointRestartVoteState::Rejected;
	}

	LastCheckpointRestartVoteResult = Result;
	UGameplayStatics::SetGamePaused(this, false);
	CheckpointRestartVote.Reset();
	CheckpointRestartVoteLayerOwner.Reset();

	UE_LOG(LogTemp, Log,
		TEXT("[Checkpoint.RestartVote] Finished Result=%d"),
		static_cast<int32>(Result));
}

bool AOutlierGameMode::StartCheckpointRestart(
	AFirstPersonPlayerController* Requester)
{
	UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	AOutlierPlayerState* RequesterPlayerState = Requester
		? Requester->GetPlayerState<AOutlierPlayerState>()
		: nullptr;
	FOutlierCheckpointSnapshot Snapshot;
	if (!SaveSubsystem
		|| !ArenaSubsystem
		|| !RequesterPlayerState
		|| !ShooterClass
		|| !PartnerClass
		|| (ArenaSubsystem->IsGameplayDataLayerAvailable()
			&& ArenaSubsystem->GetGameplayReloadPhase() != EOutlierGameplayReloadPhase::Ready)
		|| !SaveSubsystem->GetRestoreSnapshot(Snapshot))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Checkpoint.Restart] Preflight failed Requester=%s Save=%d Arena=%d ShooterClass=%d PartnerClass=%d"),
			*GetNameSafe(Requester),
			SaveSubsystem ? 1 : 0,
			ArenaSubsystem ? 1 : 0,
			ShooterClass ? 1 : 0,
			PartnerClass ? 1 : 0);
		return false;
	}

	const int32 PairId = RequesterPlayerState->GetPairId();
	AOutlierPlayerState* ShooterPlayerState = RequesterPlayerState->IsShooterPlayer()
		? RequesterPlayerState
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Shooter);
	AOutlierPlayerState* PartnerPlayerState = RequesterPlayerState->IsPartnerPlayer()
		? RequesterPlayerState
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Partner);
	if (!ShooterPlayerState || !PartnerPlayerState
		|| !GetControllerFromPlayerState(ShooterPlayerState)
		|| !GetControllerFromPlayerState(PartnerPlayerState))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Checkpoint.Restart] Pair preflight failed PairId=%d ShooterPS=%s PartnerPS=%s"),
			PairId,
			*GetNameSafe(ShooterPlayerState),
			*GetNameSafe(PartnerPlayerState));
		return false;
	}

	if (UEnemyAdaptationSubsystem* EnemyAdaptationSubsystem =
		GetWorld()->GetSubsystem<UEnemyAdaptationSubsystem>())
	{
		const int32 StackBeforeRestore =
			EnemyAdaptationSubsystem->GetCurrentGunAdaptationStack();
		if (EnemyAdaptationSubsystem->SetGunAdaptationStack(
			Snapshot.GunAdaptationStack))
		{
			UE_LOG(
				LogOutlier,
				Display,
				TEXT("[Checkpoint] Enemy adaptation restored Checkpoint=%s PreviousStack=%d RestoredStack=%d"),
				*Snapshot.CheckpointId.ToString(),
				StackBeforeRestore,
				EnemyAdaptationSubsystem->GetCurrentGunAdaptationStack());
		}
		else
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[Checkpoint] Enemy adaptation restore skipped Checkpoint=%s SavedStack=%d"),
				*Snapshot.CheckpointId.ToString(),
				Snapshot.GunAdaptationStack);
		}
	}

	// 새 Actor가 BeginPlay에서 읽는 월드 진행과 공유 내성 Stack을 먼저 되돌린 뒤
	// Data Layer를 내린다. 순서를 뒤집으면 새 Actor가 재시작 직전 상태를 잠깐 적용한다.
	SaveSubsystem->RestoreCurrentWorldProgress(Snapshot.WorldProgress);
	SaveSubsystem->RestoreCurrentDestroyedTurretIds(Snapshot.DestroyedTurretIds);
	ShooterPlayerState->RestoreCheckpointProgress(
		Snapshot.ShooterProgress.NodeCount,
		EOutlierUpgradeRole::Shooter,
		Snapshot.ShooterProgress.ActivatedUpgradeNodeIds);
	PartnerPlayerState->RestoreCheckpointProgress(
		Snapshot.PartnerProgress.NodeCount,
		EOutlierUpgradeRole::Partner,
		Snapshot.PartnerProgress.ActivatedUpgradeNodeIds);
	ShooterPlayerState->SetAcquiredSuit(Snapshot.SuitSnapshot.bAcquired);
	ShooterPlayerState->SetSuitMeshes(
		Snapshot.SuitSnapshot.FirstPersonMesh,
		Snapshot.SuitSnapshot.ThirdPersonMesh);
	ShooterPlayerState->SetLoadoutSnapshot(Snapshot.LoadoutSnapshot);

	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		EnemyRoomSubsystem->ResetRuntimeCombatState();
	}
	if (URoomCombatSubsystem* RoomCombatSubsystem =
		GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		RoomCombatSubsystem->ResetRuntimeCombatState();
	}

	// Listen Server의 전역 Pause는 World Partition의 Data Layer 전환도 멈춘다.
	// 기존 Pawn은 아래 리로드에서 즉시 제거되고 새 Pawn은 준비 완료 전까지 Possess하지 않으므로,
	// Restarting 상태 자체로 플레이어 입력을 막은 채 월드만 다시 진행시켜 EndPlay/GC 이벤트를 받는다.
	UGameplayStatics::SetGamePaused(this, false);

	return ReloadArenaAndRespawnPair(
		ShooterPlayerState,
		PartnerPlayerState,
		Snapshot.ShooterSpawnTransform,
		Snapshot.PartnerSpawnTransform,
		/*bRestoreCheckpointSnapshot=*/true);
}

void AOutlierGameMode::FinishCheckpointRestart()
{
	if (!bCheckpointRestartInProgress)
	{
		return;
	}

	bCheckpointRestartInProgress = false;
	LastCheckpointRestartVoteResult = EOutlierCheckpointRestartVoteState::Idle;
	CheckpointRestartVote.Reset();
	CheckpointRestartVoteLayerOwner.Reset();
	UGameplayStatics::SetGamePaused(this, false);
	UE_LOG(LogTemp, Log, TEXT("[Checkpoint.Restart] Reload completed; gameplay resumed"));
}

void AOutlierGameMode::CancelCheckpointRestartVoteForDisconnect(
	APlayerController* ExitingPlayer)
{
	if (CheckpointRestartVote.GetState() == EOutlierCheckpointRestartVoteState::VotePending
		&& CheckpointRestartVote.Contains(ExitingPlayer))
	{
		FinishCheckpointRestartVote(EOutlierCheckpointRestartVoteState::Rejected);
	}
}

void AOutlierGameMode::InitGame(
	const FString& MapName,
	const FString& Options,
	FString& ErrorMessage)
{
	// 역할을 못 읽는 경우의 폴백 클래스. 정상 경로에서는 SpawnPlayerController 오버라이드가
	// 접속 URL(?Role=Shooter)을 보고 역할별 Controller를 직접 스폰하므로 여기까지 오지 않는다.
	// 역할별 BP Controller를 바로 만들면 BeginPlay에서 역할 HUD가 먼저 뜨는 문제가 있어
	// 원래는 공통 Controller로 받았다가 교체했었는데, 그 교체가 dedi에서 WP 셀 가시화 RPC를
	// 폐기시키는 원인이라 제거했다.
	if (IsArenaWorkerProcess())
	{
		PlayerControllerClass = AFirstPersonPlayerController::StaticClass();
	}

	Super::InitGame(MapName, Options, ErrorMessage);
}

void AOutlierGameMode::InitGameState()
{
	Super::InitGameState();
	PauseArenaWorkerWorld();
}

void AOutlierGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CheckpointRestartVote.Reset();
	CheckpointRestartVoteLayerOwner.Reset();
	ClearArenaGameplayReloadDelegates();
	GetWorldTimerManager().ClearTimer(ArenaWorkerReloadFailureTimerHandle);
	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
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
	ClearListenReconnectPawn();
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

bool AOutlierGameMode::IsArenaWorkerPreloadReady() const
{
	return ArenaWorkerPreloadSource
		&& ArenaWorkerPreloadSource->IsStreamingCompleted();
}

void AOutlierGameMode::PauseArenaWorkerWorld()
{
	constexpr float ArenaWorkerPreloadRadius = 12800.0f; // 128 m (UE unit = cm)

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

	APresetPlayerStart* StreamingStart = nullptr;
	TArray<AActor*> PresetStarts;
	UGameplayStatics::GetAllActorsOfClass(
		World, APresetPlayerStart::StaticClass(), PresetStarts);
	for (AActor* Actor : PresetStarts)
	{
		APresetPlayerStart* PresetStart = Cast<APresetPlayerStart>(Actor);
		if (PresetStart
			&& PresetStart->GetPresetId() == OutlierPresetStageIds::Start
			&& PresetStart->GetLevel() == World->PersistentLevel)
		{
			StreamingStart = PresetStart;
			break;
		}
	}

	if (!StreamingStart)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaWorker] Persistent PresetStart with PresetId=Start was not found; cannot create preload source"));
		return;
	}

	const FTransform StreamingStartTransform = StreamingStart->GetActorTransform();

	AOutlierArenaPausePlayerState* PauseOwner =
		World->SpawnActorDeferred<AOutlierArenaPausePlayerState>(
			AOutlierArenaPausePlayerState::StaticClass(),
			StreamingStartTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!PauseOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaWorker] Failed to create the pre-match pause owner"));
		return;
	}

	PauseOwner->SetFlags(RF_Transient);
	UGameplayStatics::FinishSpawningActor(PauseOwner, StreamingStartTransform);

	// Worker는 PlayerController streaming source가 생기기 전에 WP 셀을 로드해야 한다.
	// 항상 로드되는 Start Preset 위치에서 128m 범위의 셀과 중첩 LevelInstance를 먼저 올린다.
	ArenaWorkerPreloadSource = NewObject<UWorldPartitionStreamingSourceComponent>(
		PauseOwner, UWorldPartitionStreamingSourceComponent::StaticClass(), TEXT("ArenaWorkerPreloadSource"));
	if (ArenaWorkerPreloadSource)
	{
		FStreamingSourceShape PreloadShape;
		PreloadShape.bUseGridLoadingRange = false;
		PreloadShape.Radius = ArenaWorkerPreloadRadius;
		ArenaWorkerPreloadSource->Shapes.Add(PreloadShape);
		ArenaWorkerPreloadSource->RegisterComponent();
		if (UWorldPartitionSubsystem* WorldPartitionSubsystem =
			World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			WorldPartitionSubsystem->OnUpdateStreamingState();
		}
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaWorker] Preload streaming source registered Preset=%s Location=%s Radius=%.0f"),
			*GetNameSafe(StreamingStart),
			*StreamingStart->GetActorLocation().ToString(),
			PreloadShape.Radius);
	}

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
	ArenaWorkerPreloadSource = nullptr;
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

bool AOutlierGameMode::RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint)
{
	if (!HasAuthority() || !Controller || !Checkpoint || Checkpoint->GetCheckpointId().IsNone())
	{
		return false;
	}

	AOutlierPlayerState* TriggeringPS = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPS)
	{
		return false;
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
		return false;
	}

	if (AController* PartnerController = GetControllerFromPlayerState(PartnerPS);
		PartnerController && Cast<AEnemyBase>(PartnerController->GetPawn()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint] Commit rejected while Partner possesses an enemy Id=%s"),
			*Checkpoint->GetCheckpointId().ToString());
		return false;
	}
	const UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
		: nullptr;
	if (EnemyRoomSubsystem && EnemyRoomSubsystem->HasActiveCombat())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint] Commit rejected while combat is active Id=%s"),
			*Checkpoint->GetCheckpointId().ToString());
		return false;
	}
	const URoomCombatSubsystem* RoomCombatSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
		: nullptr;
	if (RoomCombatSubsystem
		&& RoomCombatSubsystem->GetActiveCombatRoomTag().IsValid())
	{
		// 생존 적이 잠시 0명이더라도 다음 Wave를 기다리는 중이면 체크포인트를 확정하지 않는다.
		UE_LOG(LogTemp, Warning,
			TEXT("[Checkpoint] Commit rejected while a Room Wave is active Id=%s"),
			*Checkpoint->GetCheckpointId().ToString());
		return false;
	}

	UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	if (!SaveSubsystem)
	{
		return false;
	}
	if (!SaveSubsystem->HasValidStableIds())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Checkpoint] Commit rejected because stable Id validation failed"));
		return false;
	}

	FOutlierCheckpointSnapshot Snapshot;
	if (!BuildPairCheckpointSnapshot(
		ShooterPS,
		PartnerPS,
		Checkpoint->GetCheckpointId(),
		false,
		Checkpoint->GetSpawnTransform(),
		Checkpoint->GetPartnerSpawnTransform(),
		Snapshot)
		|| !SaveSubsystem->CommitCheckpointSnapshot(Snapshot))
	{
		return false;
	}

	FOutlierCheckpointData Data;
	Data.LevelName = FName(*GetWorld()->GetMapName());
	Data.CheckpointId = Checkpoint->GetCheckpointId();
	ApplyCheckpointToPair(TriggeringPS, Data);
	return true;
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

	// 프로세스 종류로 미리 자르지 않는다. ArenaWorker는 이제 실제 두 플레이어가 플레이하는
	// 프로세스라, IsArenaWorkerProcess()로 끊으면 dedi에서만 PreSetLoadWidget이 안 뜨고
	// 즉시 리스폰으로 폴백한다. 사람이 없는 경우(봇 전용)는 BeginPresetRespawnSelection이
	// "알릴 컨트롤러가 하나도 없으면 RespawnPairAtCheckpoint"로 이미 처리한다.
	BeginPresetRespawnSelection(Controller);
}

void AOutlierGameMode::BeginPresetRespawnSelection(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPS = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPS)
	{
		RespawnPairAtCheckpoint(Controller);
		return;
	}

	const int32 PairId = TriggeringPS->GetPairId();
	AOutlierPlayerState* ShooterPS = TriggeringPS->IsShooterPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Shooter);
	AOutlierPlayerState* PartnerPS = TriggeringPS->IsPartnerPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Partner);

	bool bNotifiedAny = false;
	if (AFirstPersonPlayerController* ShooterFPC = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(ShooterPS)))
	{
		ShooterFPC->Client_ShowPresetSelect();
		bNotifiedAny = true;
	}
	if (AFirstPersonPlayerController* PartnerFPC = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(PartnerPS)))
	{
		PartnerFPC->Client_ShowPresetSelect();
		bNotifiedAny = true;
	}

	if (!bNotifiedAny)
	{
		RespawnPairAtCheckpoint(Controller);
	}
}

void AOutlierGameMode::HandlePresetStageSelected(AController* Requester, FName StageId)
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

	TriggeringPS->SetPendingPresetSelection(StageId);

	if (StageId == NAME_None)
	{
		// Start를 포함해 위젯의 모든 버튼은 이제 실제 스테이지 FName을 보낸다.
		// 여기 걸리는 건 위젯이 아닌 다른 경로에서 빈 FName을 보낸 비정상 케이스뿐이니 대기만 한다.
		return;
	}

	const int32 PairId = TriggeringPS->GetPairId();
	AOutlierPlayerState* OtherPS = TriggeringPS->IsShooterPlayer()
		? FindPairPlayerState(PairId, EOutlierPlayerRole::Partner)
		: TriggeringPS->IsPartnerPlayer()
			? FindPairPlayerState(PairId, EOutlierPlayerRole::Shooter)
			: nullptr;

	// 페어 상대가 아직 다른 스테이지를 고르는 중이면 대기. 상대가 아예 없으면(봇/아레나워커) 혼자 진행.
	if (OtherPS && OtherPS->GetPendingPresetSelection() != StageId)
	{
		return;
	}

	TriggeringPS->SetPendingPresetSelection(NAME_None);
	if (OtherPS)
	{
		OtherPS->SetPendingPresetSelection(NAME_None);
	}

	// 합의 성립 - 양쪽 위젯을 닫는다. ClientPopInGameSettingLayer는 이름과 달리 범용 pop-by-owner RPC라
	// Push 때 RequestOwner로 넘겼던 PlayerState를 그대로 넘기면 어떤 위젯이든 닫힌다.
	if (AFirstPersonPlayerController* TriggeringFPC = Cast<AFirstPersonPlayerController>(Requester))
	{
		TriggeringFPC->ClientPopInGameSettingLayer(TriggeringPS);
	}
	if (AFirstPersonPlayerController* OtherFPC = Cast<AFirstPersonPlayerController>(GetControllerFromPlayerState(OtherPS)))
	{
		OtherFPC->ClientPopInGameSettingLayer(OtherPS);
	}

	RequestPresetRespawn(Requester, StageId);
}

int32 AOutlierGameMode::ResolvePresetNodeCount(FName StageId) const
{
	// 테이블은 GameMode BP가 아니라 프로젝트 설정에서 읽는다. GameMode 파생 BP가 3개라
	// (BP_OutlierGM / BP_LobbyGM / BP_BTestGM) 실행 형태마다 다른 BP가 떠서, 한 곳만 꽂아두면
	// 다른 쪽에서는 조용히 0이 됐다. 특히 Listen은 Title 맵을 열고 아레나를 스트리밍으로 얹으므로
	// GameMode가 BP_LobbyGM이고, 아레나 맵의 World Settings에 꽂은 값은 쓰이지 않는다.
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!Settings || Settings->PresetNodeProvideTable.IsNull())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PresetNode] PresetNodeProvideTable is not set (Project Settings > Outlier Arena) — Stage=%s NodeCount=0"),
			*StageId.ToString());
		return 0;
	}

	const UDataTable* NodeProvideTable = Settings->PresetNodeProvideTable.LoadSynchronous();
	if (!NodeProvideTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PresetNode] Failed to load %s — Stage=%s NodeCount=0"),
			*Settings->PresetNodeProvideTable.ToSoftObjectPath().ToString(), *StageId.ToString());
		return 0;
	}

	static const FString ContextString(TEXT("ResolvePresetNodeCount"));
	if (const FPresetNodeProvideRow* Row = NodeProvideTable->FindRow<FPresetNodeProvideRow>(StageId, ContextString))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PresetNode] Stage=%s Count=%d"),
			*StageId.ToString(), Row->Count);
		return Row->Count;
	}

	// DT_PresetNode의 행은 Level1~Level4다(PresetNodeProvideRow.h 주석). Start처럼 행이 없는
	// StageId로 들어오면 여기로 떨어져 NodeCount가 0이 된다 — 의도한 것인지 확인할 것.
	UE_LOG(LogTemp, Warning,
		TEXT("[PresetNode] Row not found in %s for Stage=%s — NodeCount=0"),
		*GetNameSafe(NodeProvideTable), *StageId.ToString());
	return 0;
}

void AOutlierGameMode::FlushUpgradeNodesForPair(AOutlierPlayerState* TriggeringPlayerState, int32 NewNodeCount)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	const int32 PairId = TriggeringPlayerState->GetPairId();

	if (PairId == INDEX_NONE || !GameState)
	{
		TriggeringPlayerState->FlushActivatedUpgradeNodes(NewNodeCount);
		return;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PairPlayerState = Cast<AOutlierPlayerState>(RawPlayerState);
		if (!PairPlayerState || PairPlayerState->GetPairId() != PairId)
		{
			continue;
		}

		PairPlayerState->FlushActivatedUpgradeNodes(NewNodeCount);
	}
}

bool AOutlierGameMode::ResolvePresetStageSpawn(
	FName StageId,
	FTransform& OutShooterSpawn,
	FTransform& OutPartnerSpawn) const
{
	if (StageId == NAME_None)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UOutlierArenaSubsystem* ArenaSubsystem = World ? World->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!ArenaSubsystem)
	{
		return false;
	}

	TArray<AActor*> PresetStarts;
	UGameplayStatics::GetAllActorsOfClass(World, APresetPlayerStart::StaticClass(), PresetStarts);

	bool bFoundMatchingId = false;

	for (AActor* Actor : PresetStarts)
	{
		const APresetPlayerStart* PresetStart = Cast<APresetPlayerStart>(Actor);
		if (!PresetStart || PresetStart->GetPresetId() != StageId)
		{
			continue;
		}

		bFoundMatchingId = true;

		if (!ArenaSubsystem->IsActorOwnedByArena(PresetStart))
		{
			// 어느 아레나 풀 인스턴스(WP 셀) 소속인지 못 찾음 — 퍼시스턴트 레벨에 놓였거나
			// 그 아레나가 아직 이 클라이언트/서버에 로드되지 않은 경우. INDEX_NONE을 그대로
			// Arena 밖의 시작점을 사용하면 리로드 뒤 소유 관계가 보장되지 않으므로 걸러낸다.
			UE_LOG(LogTemp, Warning,
				TEXT("[PresetRespawn] APresetPlayerStart '%s' (PresetId=%s) is not owned by the Arena"),
				*GetNameSafe(PresetStart), *StageId.ToString());
			continue;
		}

		OutShooterSpawn = PresetStart->GetActorTransform();
		OutPartnerSpawn = OutShooterSpawn;
		OutPartnerSpawn.AddToTranslation(OutShooterSpawn.GetRotation().GetRightVector() * 150.0f);

		return true;
	}

	if (!bFoundMatchingId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetRespawn] No APresetPlayerStart with PresetId=%s exists in the world"), *StageId.ToString());
	}

	return false;
}

void AOutlierGameMode::RequestPresetRespawn(AController* Requester, FName StageId)
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

	FTransform ShooterSpawn;
	FTransform PartnerSpawn;
	if (!ResolvePresetStageSpawn(StageId, ShooterSpawn, PartnerSpawn))
	{
		// 프리셋을 못 찾았다고 리스폰을 포기하면 플레이어가 죽은 채로 방치된다.
		UE_LOG(LogTemp, Warning,
			TEXT("[PresetRespawn] No APresetPlayerStart for StageId=%s; falling back to a PlayerStart"),
			*StageId.ToString());
		ResolveFallbackSpawnTransforms(Requester, ShooterSpawn, PartnerSpawn);
	}

	const int32 NewNodeCount = ResolvePresetNodeCount(StageId);
	FlushUpgradeNodesForPair(TriggeringPS, NewNodeCount);

	ReloadArenaAndRespawnPair(ShooterPS, PartnerPS, ShooterSpawn, PartnerSpawn);
}

void AOutlierGameMode::StartMatchedPair(AController* FirstController, AController* SecondController, int32 PairId, EOutlierPlayerRole FirstRole, EOutlierPlayerRole SecondRole)
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

	if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		SaveSubsystem->ResetRuntimeCheckpointState();
	}

	FirstPS->SetPairId(PairId);
	FirstPS->SetPlayerRole(FirstRole);
	FirstPS->ClearPendingLobbyState();

	SecondPS->SetPairId(PairId);
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

	// 로비 -> WP 최초 진입은 이제 이 아레나 인스턴스 소속의 PresetId=Start APresetPlayerStart를 우선 찾는다.
	// 아직 레벨에 안 놔뒀으면(구 맵) 기존 ResolveArenaSpawnTransforms/FindPlayerStart 폴백으로 내려간다.
	if (!ResolvePresetStageSpawn(OutlierPresetStageIds::Start, ShooterSpawn, PartnerSpawn))
	{
		ResolveFallbackSpawnTransforms(ShooterController, ShooterSpawn, PartnerSpawn);
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

	// 스폰 지점 바닥이 아직 스트리밍 안 된 아레나(주로 원점이 아닌 아레나)에서 낙사하는 것을 막는다.
	// (PlayerStart 자체는 Is Spatially Loaded=false라 무죄, 문제는 그 아래 일반 셀 소속 바닥)
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(Shooter);
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(Partner);
	}

	APlayerController* NewShooterPC = nullptr;
	APlayerController* NewPartnerPC = nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// ArenaWorker는 로그인 시점에 이미 역할별 PC로 들어온다(SpawnPlayerController 오버라이드).
	// 그 경우 교체하지 않고 그대로 쓴다. 교체하면 교체 창 동안 클라 월드에 소유 커넥션 없는
	// 옛 PC가 남고, 하필 그 PC로 WP 셀 가시화 RPC가 나가 폐기된다. 엔진에 재전송이 없어서
	// (LevelStreaming.cpp의 ClientPendingRequestIndex 대기) 클라 스트리밍이 영구 정지한다.
	// 로비(Frontend PC)에서 시작하는 경로는 여전히 교체가 필요하므로 클래스로 판정한다.
	APlayerController* ExistingShooterPC = Cast<APlayerController>(ShooterController);
	APlayerController* ExistingPartnerPC = Cast<APlayerController>(PartnerController);

	const bool bShooterAlreadyCorrect =
		ExistingShooterPC && ShooterControllerClass && ExistingShooterPC->IsA(ShooterControllerClass);
	const bool bPartnerAlreadyCorrect =
		ExistingPartnerPC && PartnerControllerClass && ExistingPartnerPC->IsA(PartnerControllerClass);

	if (bShooterAlreadyCorrect)
	{
		NewShooterPC = ExistingShooterPC;
	}
	else if (ShooterControllerClass)
	{
		NewShooterPC = GetWorld()->SpawnActor<APlayerController>(ShooterControllerClass, ShooterSpawn, SpawnParams);
	}

	if (bPartnerAlreadyCorrect)
	{
		NewPartnerPC = ExistingPartnerPC;
	}
	else if (PartnerControllerClass)
	{
		NewPartnerPC = GetWorld()->SpawnActor<APlayerController>(PartnerControllerClass, PartnerSpawn, SpawnParams);
	}

	if (bShooterAlreadyCorrect && bPartnerAlreadyCorrect)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[GameMode] Controllers already match their roles; skipping SwapPlayerControllers"));
	}

	if (!bShooterAlreadyCorrect && ExistingShooterPC && NewShooterPC)
	{
		SwapPlayerControllers(ExistingShooterPC, NewShooterPC);

		if (bHasShooterPlayerId &&
			!Identity->RebindPlayer(ShooterPlayerId, NewShooterPC))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[LobbyIdentity] Shooter rebind failed: %s"),
				*ShooterPlayerId.ToString());
		}
	}

	if (!bPartnerAlreadyCorrect && ExistingPartnerPC && NewPartnerPC)
	{
		SwapPlayerControllers(ExistingPartnerPC, NewPartnerPC);

		if (bHasPartnerPlayerId &&
			!Identity->RebindPlayer(PartnerPlayerId, NewPartnerPC))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[LobbyIdentity] Failed to rebind Partner PlayerId=%s"),
				*PartnerPlayerId.ToString());
		}
	}

	AOutlierPlayerState* NewShooterPS = NewShooterPC ? NewShooterPC->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AOutlierPlayerState* NewPartnerPS = NewPartnerPC ? NewPartnerPC->GetPlayerState<AOutlierPlayerState>() : nullptr;

	if (NewShooterPS)
	{
		NewShooterPS->SetPairId(PairId);
		NewShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
		NewShooterPS->ClearPendingLobbyState();
	}

	if (NewPartnerPS)
	{
		NewPartnerPS->SetPairId(PairId);
		NewPartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
		NewPartnerPS->ClearPendingLobbyState();
	}

	if (IsArenaWorkerProcess())
	{
		ArenaWorkerShooterController = NewShooterPC;
		ArenaWorkerPartnerController = NewPartnerPC;
	}

	RegisterSpawnedPair(NewShooterPS, NewPartnerPS, Shooter, Partner);
	CaptureInitialCheckpointSnapshot(NewShooterPS, NewPartnerPS, Shooter, Partner);
	if (GetNetMode() == NM_ListenServer && Identity)
	{
		APlayerController* GuestController = NewShooterPC && !NewShooterPC->IsLocalController()
			? NewShooterPC : NewPartnerPC;
		AOutlierPlayerState* GuestState = GuestController
			? GuestController->GetPlayerState<AOutlierPlayerState>() : nullptr;
		if (GuestState && GuestState->HasValidTemporaryPlayerId())
		{
			ListenGuestReconnectToken = FGuid::NewGuid();
			ListenGuestPlayerId = GuestState->GetTemporaryPlayerId();
			ListenGuestRole = GuestState->GetPlayerRole();
			ListenGuestPairId = PairId;
			if (AFirstPersonPlayerController* Guest = Cast<AFirstPersonPlayerController>(GuestController))
			{
				Guest->ClientConfigureListenReconnect(ListenGuestReconnectToken);
			}
		}
	}

	PossessMatchedPawn(NewShooterPC, Shooter, ShooterSpawn.GetLocation());
	PossessMatchedPawn(NewPartnerPC, Partner, PartnerSpawn.GetLocation());

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
		? Settings->ResolveLobbyAddress()
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
	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	ArenaWorkerDisconnectedPlayerIds.Reset();
	ClearArenaWorkerReconnectPawns();
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
		if (AFirstPersonPlayerController* FirstPersonController =
			Cast<AFirstPersonPlayerController>(ShooterController))
		{
			FirstPersonController->ClientPrepareForArenaExit();
		}
		ShooterController->ClientTravel(LobbyAddress, TRAVEL_Absolute);
	}
	if (APlayerController* PartnerController = ArenaWorkerPartnerController.Get())
	{
		if (AFirstPersonPlayerController* FirstPersonController =
			Cast<AFirstPersonPlayerController>(PartnerController))
		{
			FirstPersonController->ClientPrepareForArenaExit();
		}
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

bool AOutlierGameMode::HandleExplicitPlayerLeave(AFirstPersonPlayerController* Requester)
{
	if (!HasAuthority() || !Requester)
	{
		return false;
	}

	CancelCheckpointRestartVoteForDisconnect(Requester);
	UGameplayStatics::SetGamePaused(this, false);

	if (IsArenaWorkerProcess())
	{
		if (bArenaWorkerMatchCompleting)
		{
			return false;
		}

		UE_LOG(LogTemp, Display,
			TEXT("[ArenaReturn] Explicit leave requested; releasing Worker Match=%s Player=%s"),
			*ArenaWorkerAdmission.MatchId.ToString(),
			*GetNameSafe(Requester));
		// MatchCompleting을 실제 연결 종료보다 먼저 세워 Logout의 재접속 유예 경로를 막는다.
		BeginArenaWorkerReleaseShutdown();
		return true;
	}

	const ENetMode NetMode = GetNetMode();
	if (NetMode != NM_Standalone && NetMode != NM_ListenServer)
	{
		return false;
	}
	if (bListenHostReturnRequested)
	{
		return false;
	}

	// 2인 Listen Match에서는 Host와 Guest 어느 쪽의 명시적 이탈이든 Match 전체를 끝낸다.
	// Standalone도 같은 엔진 경로를 사용하며 GameDefaultMap인 Title로 돌아간다.
	bListenHostReturnRequested = true;
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReturn] Explicit leave requested; returning session to Title Player=%s NetMode=%d"),
		*GetNameSafe(Requester),
		static_cast<int32>(NetMode));
	if (NetMode == NM_ListenServer)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AFirstPersonPlayerController* Guest = Cast<AFirstPersonPlayerController>(It->Get());
				Guest && !Guest->IsLocalController())
			{
				Guest->ClientPrepareForArenaExit();
			}
		}
	}
	ReturnToMainMenuHost();
	return true;
}

void AOutlierGameMode::PossessMatchedPawn(
	APlayerController* PlayerController,
	APawn* Pawn,
	const FVector& SpawnLocation)
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
				// ArenaWorker는 여기서 바로 Possess해버리므로 클라는 한동안 Pawn이 없다.
				// listen의 ClientArenaLoad와 동일하게 서버가 계산한 스폰 위치를 같이 넘겨야
				// 클라가 스트리밍 소스를 놓을 곳을 알 수 있다.
				FirstPersonController->ClientPrepareForArenaStart(SpawnLocation);
			}
		}
		PlayerController->Possess(Pawn);
		return;
	}

	PendingPossessions.Add(PlayerController, Pawn);
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(PlayerController))
	{
		// Possess 전이라 클라는 아직 자기 Pawn 위치를 모른다. 서버가 이미 계산해둔
		// 실제 스폰 위치를 같이 넘겨서, 클라가 레벨 액터를 추측해서 찾지 않게 한다.
		FirstPersonController->ClientArenaLoad(SpawnLocation, 0);
	}
}



void AOutlierGameMode::OnClientArenaReady(APlayerController* PC, uint32 ReconnectRequestId)
{
	// ACK 순서: 요청 번호 검증 -> 현재 Room/위치 재검사 -> Possess.
	// Validate가 위치를 바꾸면 새 스트리밍 요청을 보내고 여기서는 Possess하지 않는다.
	const uint32* ExpectedRequestId = PendingReconnectRequestIds.Find(PC);
	if ((ExpectedRequestId && *ExpectedRequestId != ReconnectRequestId)
		|| (!ExpectedRequestId && ReconnectRequestId != 0))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[Arena] Stale reconnect ready ignored. PC=%s Request=%u Expected=%u"),
			*GetNameSafe(PC), ReconnectRequestId, ExpectedRequestId ? *ExpectedRequestId : 0);
		return;
	}
	if (bArenaReloadInProgress && !ExpectedRequestId)
	{
		const AOutlierPlayerState* State = PC
			? PC->GetPlayerState<AOutlierPlayerState>() : nullptr;
		const bool bRejoiningListenGuest = bListenGuestDisconnected && State
			&& State->GetTemporaryPlayerId() == ListenGuestPlayerId;
		// 일반 리로드 ACK는 과거 Room을 버린다. 재접속 Guest의 첫 ACK는
		// 요청 번호가 0이어도 새 Controller의 Room 재검사가 끝날 때까지 보존한다.
		if (!bRejoiningListenGuest)
		{
			PendingReconnectContexts.Remove(PC);
		}
	}
	if (IsArenaWorkerProcess())
	{
		if (!bArenaWorkerPairStarted
			|| (PC != ArenaWorkerShooterController.Get()
				&& PC != ArenaWorkerPartnerController.Get()))
		{
			return;
		}

		// 최초 입장 시에만 Worker 매치 시작 ready를 집계한다.
		// 이미 게임이 시작된 뒤의 ready는 reload 후 새 Pawn의 possess를 위해
		// 아래 PendingPossessions 처리 경로로 내려간다.
		if (!bArenaWorkerGameplayStarted)
		{
			ArenaWorkerReadyPlayers.Add(PC);
			if (ArenaWorkerReadyPlayers.Contains(ArenaWorkerShooterController)
				&& ArenaWorkerReadyPlayers.Contains(ArenaWorkerPartnerController))
			{
				ScheduleArenaWorkerGameplayStart();
			}
			return;
		}
	}

	TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(PC);
	if (!PendingPawn || !(*PendingPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Arena] OnClientArenaReady: no pending pawn for PC=%s"), *GetNameSafe(PC));
		PendingPossessions.Remove(PC);
		return;
	}

	APawn* Pawn = PendingPawn->Get();
	if (!ValidateReconnectPawn(PC, Pawn))
	{
		return;
	}
	PendingPossessions.Remove(PC);
	PendingReconnectRequestIds.Remove(PC);

	PC->Possess(Pawn);
	if (const AOutlierPlayerState* State = PC->GetPlayerState<AOutlierPlayerState>())
	{
		const FGuid PlayerId = State->GetTemporaryPlayerId();
		ArenaWorkerDisconnectContexts.Remove(PlayerId);
		if (const bool* bWasDamageable = ArenaWorkerReconnectDamageStates.Find(PlayerId))
		{
			Pawn->SetCanBeDamaged(*bWasDamageable);
			ArenaWorkerReconnectDamageStates.Remove(PlayerId);
		}
		if (GetNetMode() == NM_ListenServer && PlayerId == ListenGuestPlayerId
			&& ListenGuestReconnectPawn == Pawn)
		{
			ListenGuestReconnectPawn = nullptr;
			bListenGuestDisconnected = false;
			UE_LOG(LogTemp, Display, TEXT("[ListenReconnect] Guest rejoined after Room streaming"));
		}
	}
	if (ArenaWorkerDisconnectedPlayerIds.IsEmpty() && PendingReconnectContexts.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	}
	TryFinishArenaReload();
}

void AOutlierGameMode::OnClientArenaGameplayGCReady(APlayerController* PC, uint32 GameplayGeneration)
{
	// 이번 세대의 대기 참가자만 집계한다. 모두 ACK해도 서버 자체의 GC 검증은 Subsystem에서 별도로 기다린다.
	if (!PC
		|| GameplayGeneration == 0
		|| GameplayGeneration != PendingGameplayGeneration
		|| !PendingGameplayGCPlayers.Contains(PC))
	{
		return;
	}

	ReadyGameplayGCPlayers.Add(PC);
	if (ReadyGameplayGCPlayers.Num() < PendingGameplayGCPlayers.Num())
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[Arena][DataLayer] All remote clients completed GC Generation=%u; activating Gameplay layer"),
		GameplayGeneration);

	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		ArenaSubsystem->ActivateGameplayData(GameplayGeneration);
	}

	PendingGameplayGCPlayers.Reset();
	ReadyGameplayGCPlayers.Reset();
}

void AOutlierGameMode::ArenaRetryGameplayReload()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		if (ArenaSubsystem->RetryStalledGameplayReload(PendingGameplayGeneration))
		{
			for (const TWeakObjectPtr<APlayerController>& Player : PendingGameplayGCPlayers)
			{
				if (AFirstPersonPlayerController* FirstPersonController =
					Cast<AFirstPersonPlayerController>(Player.Get()))
				{
					FirstPersonController->ClientRetryArenaGameplayReload(PendingGameplayGeneration);
				}
			}
		}
	}
}

void AOutlierGameMode::ArenaDumpGameplayReload()
{
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr)
	{
		ArenaSubsystem->DumpGameplayReloadState();
	}
}

APlayerController* AOutlierGameMode::SpawnPlayerController(
	ENetRole InRemoteRole,
	const FString& Options)
{
	if (IsListenReconnectRequest(Options))
	{
		const TSubclassOf<APlayerController> RoleClass = ListenGuestRole == EOutlierPlayerRole::Shooter
			? ShooterControllerClass : PartnerControllerClass;
		if (RoleClass)
		{
			return SpawnPlayerControllerCommon(InRemoteRole, FVector::ZeroVector,
				FRotator::ZeroRotator, RoleClass);
		}
	}
	// 역할은 로비가 정해서 접속 URL에 실어 보낸다(OutlierArenaHandoff::BuildTravelURL).
	// 여기서 바로 역할별 PC를 스폰하면 StartMatchedPair의 컨트롤러 교체가 불필요해진다.
	if (IsArenaWorkerProcess())
	{
		FOutlierArenaHandoffRequest Request;
		FString ParseError;
		if (OutlierArenaHandoff::TryParseOptions(Options, Request, ParseError))
		{
			TSubclassOf<APlayerController> RoleControllerClass = nullptr;
			if (Request.Role == EOutlierPlayerRole::Shooter)
			{
				RoleControllerClass = ShooterControllerClass;
			}
			else if (Request.Role == EOutlierPlayerRole::Partner)
			{
				RoleControllerClass = PartnerControllerClass;
			}

			if (RoleControllerClass)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[ArenaWorker] Spawning role controller at login Role=%d Class=%s"),
					static_cast<int32>(Request.Role),
					*RoleControllerClass->GetName());

				return SpawnPlayerControllerCommon(
					InRemoteRole,
					FVector::ZeroVector,
					FRotator::ZeroRotator,
					RoleControllerClass);
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaWorker] Role controller class is not set (Role=%d); falling back to %s"),
				static_cast<int32>(Request.Role),
				*GetNameSafe(PlayerControllerClass));
		}
		else
		{
			// 역할을 못 읽으면 기존 공용 PC로 폴백한다. 이 경우 예전처럼 교체 경로가 필요하다.
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaWorker] Could not resolve role from login options (%s); falling back to %s"),
				*ParseError,
				*GetNameSafe(PlayerControllerClass));
		}
	}

	return Super::SpawnPlayerController(InRemoteRole, Options);
}

void AOutlierGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty() && GetNetMode() == NM_ListenServer
		&& ListenGuestReconnectToken.IsValid()
		&& !IsListenReconnectRequest(Options))
	{
		ErrorMessage = TEXT("Listen match is reserved for its original guest");
		return;
	}

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
	if (IsArenaWorkerReconnectRequest(Request))
	{
		const bool bRoleStillConnected = Request.Role == EOutlierPlayerRole::Shooter
			? ArenaWorkerShooterController.IsValid()
			: ArenaWorkerPartnerController.IsValid();
		if (bRoleStillConnected)
		{
			ErrorMessage = TEXT("Arena player is already connected");
			return;
		}
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
	if (ErrorMessage.IsEmpty() && IsListenReconnectRequest(Options))
	{
		UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>() : nullptr;
		AOutlierPlayerState* State = NewPlayerController
			? NewPlayerController->GetPlayerState<AOutlierPlayerState>() : nullptr;
		if (!Identity || !State || !Identity->RebindPlayer(ListenGuestPlayerId, NewPlayerController))
		{
			return TEXT("Failed to restore listen guest identity");
		}
		State->SetPlayerRole(ListenGuestRole);
		State->SetPairId(ListenGuestPairId);
		return FString();
	}
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
	PlayerState->SetPairId(0);
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
	const AOutlierPlayerState* State = NewPlayer
		? NewPlayer->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (bListenGuestDisconnected && State
		&& State->GetTemporaryPlayerId() == ListenGuestPlayerId)
	{
		// 기존 Pawn을 스트리밍 ACK 뒤에 돌려주므로 기본 PlayerStart Spawn은 건너뛴다.
		return;
	}
	if (IsArenaWorkerProcess())
	{
		// StartMatchedPair에서 Pawn 생성과 Possess를 처리하므로 기본 Spawn은 생략.
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AOutlierGameMode::Logout(AController* Exiting)
{
	APlayerController* ExitingPlayer = Cast<APlayerController>(Exiting);
	CancelCheckpointRestartVoteForDisconnect(ExitingPlayer);
	// Frontend PC가 역할별 Gameplay PC로 교체될 때도 Logout이 호출된다. 실제 플레이 중인
	// 로컬 FirstPerson PC만 Host 이탈로 봐야 정상적인 Listen 시작을 세션 종료로 오인하지 않는다.
	const bool bListenHostLeaving = GetNetMode() == NM_ListenServer
		&& !bListenHostReturnRequested
		&& ExitingPlayer
		&& ExitingPlayer->IsLocalController()
		&& Cast<AFirstPersonPlayerController>(ExitingPlayer);
	FGuid ExitingArenaPlayerId;
	const bool bWaitForArenaWorkerReconnect = IsArenaWorkerProcess()
		&& bArenaWorkerPairStarted
		&& !bArenaWorkerMatchCompleting
		&& ExitingPlayer
		&& (ExitingPlayer == ArenaWorkerShooterController.Get()
			|| ExitingPlayer == ArenaWorkerPartnerController.Get())
		&& ArenaWorkerAdmission.MatchId.IsValid()
		&& (ExitingArenaPlayerId = ExitingPlayer->GetPlayerState<AOutlierPlayerState>()
			? ExitingPlayer->GetPlayerState<AOutlierPlayerState>()->GetTemporaryPlayerId()
			: FGuid()).IsValid();
	const AOutlierPlayerState* LeavingState = ExitingPlayer
		? ExitingPlayer->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bKeepListenGuest = GetNetMode() == NM_ListenServer
		&& !bListenHostReturnRequested && !bListenHostLeaving
		&& ListenGuestReconnectToken.IsValid() && LeavingState
		&& LeavingState->GetTemporaryPlayerId() == ListenGuestPlayerId;

	if (bArenaReloadInProgress && !bWaitForArenaWorkerReconnect && !bKeepListenGuest)
	{
		if (ExitingPlayer && PendingGameplayGCPlayers.Contains(ExitingPlayer))
		{
			if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
				? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
				: nullptr)
			{
				ArenaSubsystem->FailGameplayReload(
					PendingGameplayGeneration,
					EOutlierGameplayReloadFailure::RequiredClientDisconnected);
			}
		}
	}
	if (bWaitForArenaWorkerReconnect)
	{
		// 시작된 Worker 매치는 좌석과 MatchId를 해제하지 않는다. 유예 시간 동안 같은 신원이
		// 돌아오면 새 Controller를 원래 Role에 다시 연결하고, 다른 참가자는 계속 거부한다.
		// 진행 정보와 Room 수명을 기록한 다음 Pawn을 분리한다. 순서를 바꾸면
		// Logout에서 사라지는 PlayerState 또는 Controller의 정보를 놓칠 수 있다.
		AOutlierPlayerState* ExitingPlayerState = ExitingPlayer->GetPlayerState<AOutlierPlayerState>();
		AOutlierPlayerState* RemainingPlayerState = ExitingPlayerState
			? FindPairPlayerState(
				ExitingPlayerState->GetPairId(),
				ExitingPlayerState->IsShooterPlayer()
					? EOutlierPlayerRole::Partner
					: EOutlierPlayerRole::Shooter)
			: nullptr;
		if (RemainingPlayerState && ExitingPlayerState->IsShooterPlayer())
		{
			// 진행 정보의 원본인 Shooter PS가 먼저 사라지는 경우 Partner PS를 임시 보관소로 쓴다.
			// 반대 순서는 살아 있는 Shooter PS가 이미 같은 정보를 가지고 있으므로 복사가 필요 없다.
			RemainingPlayerState->CopyReconnectGameplayStateFrom(*ExitingPlayerState);
		}
		if (AOutlierPlayerState* ProgressSource = RemainingPlayerState
			? RemainingPlayerState : ExitingPlayerState)
		{
			ArenaWorkerReconnectGameplayState = ProgressSource->CaptureReconnectGameplayState();
			bHasArenaWorkerReconnectGameplayState = true;
		}
		ArenaWorkerDisconnectedPlayerIds.Add(ExitingArenaPlayerId);
		if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			FRoomCombatReconnectContext Context;
			if (Combat->GetReconnectContext(Context))
			{
				ArenaWorkerDisconnectContexts.Add(ExitingArenaPlayerId, Context);
			}
		}
		if (TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(ExitingPlayer))
		{
			// 리로드 중 Pawn은 아직 Possess되지 않아 Controller Map에만 매달려 있다.
			// 이전 Controller가 파괴되기 전에 PlayerId 키로 옮겨둬야 재접속 PC에 다시 연결할 수 있다.
			ArenaWorkerReconnectPawns.Add(ExitingArenaPlayerId, *PendingPawn);
			PendingPossessions.Remove(ExitingPlayer);
		}
		else if (!bArenaReloadInProgress)
		{
			// 재접속은 사망/체크포인트 재시작이 아니다. Pawn을 Controller에서 분리해
			// 현재 체력·탄약·장비를 보존하고, 새 Controller의 스트리밍 ACK 후 다시 Possess한다.
			if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(ExitingPlayer))
			{
				if (Cast<AEnemyBase>(PartnerController->GetPawn()))
				{
					PartnerController->ReleaseEnemyPossession();
				}
			}
			if (AFirstPersonCharacter* Pawn = Cast<AFirstPersonCharacter>(ExitingPlayer->GetPawn()))
			{
				if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(Pawn))
				{
					Shooter->ClearInputIntent();
				}
				ArenaWorkerReconnectDamageStates.Add(ExitingArenaPlayerId, Pawn->CanBeDamaged());
				Pawn->SetCanBeDamaged(false);
				ExitingPlayer->UnPossess();
				ArenaWorkerReconnectPawns.Add(ExitingArenaPlayerId, Pawn);
			}
		}
		PendingLocalPossessions.Remove(ExitingPlayer);
	}
	if (bKeepListenGuest)
	{
		// 네트워크 장애는 게임 나가기와 다르다. Host의 진행 상태와 Guest Pawn을 보존하고
		// 새 Controller의 Room/WP 준비가 끝날 때까지 피해와 입력만 잠시 멈춘다.
		AOutlierPlayerState* HostState = FindPairPlayerState(ListenGuestPairId,
			ListenGuestRole == EOutlierPlayerRole::Shooter
				? EOutlierPlayerRole::Partner : EOutlierPlayerRole::Shooter);
		if (HostState && ListenGuestRole == EOutlierPlayerRole::Shooter)
		{
			HostState->CopyReconnectGameplayStateFrom(*LeavingState);
		}
		if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			FRoomCombatReconnectContext Context;
			if (Combat->GetReconnectContext(Context))
			{
				ArenaWorkerDisconnectContexts.Add(ListenGuestPlayerId, Context);
			}
		}
		// Reload 도중이면 새 Pawn이 Possess 대기 목록에 있고, 평소에는 기존 Pawn을 분리한다.
		if (TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(ExitingPlayer))
		{
			ListenGuestReconnectPawn = *PendingPawn;
			if (AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(PendingPawn->Get()))
			{
				ArenaWorkerReconnectDamageStates.Add(ListenGuestPlayerId, Character->CanBeDamaged());
				Character->SetCanBeDamaged(false);
			}
			PendingPossessions.Remove(ExitingPlayer);
		}
		else if (APawn* Pawn = ExitingPlayer->GetPawn())
		{
			if (APartnerPlayerController* Partner = Cast<APartnerPlayerController>(ExitingPlayer);
				Partner && Cast<AEnemyBase>(Pawn))
			{
				Partner->ReleaseEnemyPossession();
				Pawn = Partner->GetPawn();
			}
			if (AFirstPersonCharacter* Character = Cast<AFirstPersonCharacter>(Pawn))
			{
				if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(Character))
				{
					Shooter->ClearInputIntent();
				}
				ArenaWorkerReconnectDamageStates.Add(ListenGuestPlayerId, Character->CanBeDamaged());
				Character->SetCanBeDamaged(false);
				ExitingPlayer->UnPossess();
				ListenGuestReconnectPawn = Character;
			}
		}
		bListenGuestDisconnected = IsValid(ListenGuestReconnectPawn.Get());
		// 이전 연결의 GC ACK는 새 연결을 대신할 수 없다.
		PendingGameplayGCPlayers.Remove(ExitingPlayer);
		ReadyGameplayGCPlayers.Remove(ExitingPlayer);
		UE_LOG(LogTemp, Display, TEXT("[ListenReconnect] Guest disconnected. PawnPreserved=%d"),
			bListenGuestDisconnected);
	}

	ArenaWorkerPlayers.Remove(ExitingPlayer);
	ArenaWorkerReadyPlayers.Remove(ExitingPlayer);

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

	if (Exiting && !bKeepListenGuest && !ArenaWorkerReconnectPawns.Contains(ExitingArenaPlayerId))
	{
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(Exiting->GetPawn()))
		{
			ShooterCharacter->CleanupOwnedWeapons();
		}

		if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(Exiting))
		{
			if (AEnemyBase* EnemyPawn = Cast<AEnemyBase>(Exiting->GetPawn()))
			{
				// 캐시를 먼저 비워야 한다. 아래 Destroy()가 트리거하는 PawnPendingDestroy() 안전장치가
				// 이미 지워질 캐시된 Partner로 복원하려고 시도하는 걸 막기 위함.
				// AI에게 돌려주지 않고 바로 지우는 건, 일반 Shooter/Partner 로그아웃과 동일하게
				// 빙의 중이던 Pawn과 원래 Pawn이 둘 다 사라지는 쪽이 자연스럽다는 판단.
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

	if (bListenHostLeaving)
	{
		bListenHostReturnRequested = true;
		UE_LOG(LogTemp, Warning, TEXT("[Arena] Listen host left; returning the session to Title"));
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AFirstPersonPlayerController* Guest = Cast<AFirstPersonPlayerController>(It->Get());
				Guest && !Guest->IsLocalController())
			{
				Guest->ClientPrepareForArenaExit();
			}
		}
		// Super::Logout이 연결을 정리하기 전에 호출해야 남아 있는 Remote PC에도
		// ReturnToMainMenu RPC를 보내고 Host 자신도 같은 흐름으로 Title에 돌아갈 수 있다.
		ReturnToMainMenuHost();
	}

	Super::Logout(Exiting);
	PendingReconnectContexts.Remove(ExitingPlayer);
	PendingReconnectRequestIds.Remove(ExitingPlayer);

	if (bListenHostLeaving)
	{
		return;
	}

	if (bWaitForArenaWorkerReconnect)
	{
		ScheduleArenaWorkerReconnectTimeout();
	}

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

	FTransform SpawnTransform;
	FTransform PartnerSpawnTransform;

	if (ResolveCheckpointTransform(GetControllerFromPlayerState(ShooterPlayerState), SpawnTransform))
	{
		PartnerSpawnTransform = SpawnTransform;
		PartnerSpawnTransform.AddToTranslation(
			SpawnTransform.GetRotation().GetRightVector() * 150.0f
		);
	}
	else if (ResolveArenaSpawnTransforms(SpawnTransform, PartnerSpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Respawn] Checkpoint missing. Fallback to arena start. Spawn=%s PartnerSpawn=%s"),
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
			TEXT("[Respawn] Arena fallback failed. Fallback to FindPlayerStart. PlayerStart=%s Spawn=%s"),
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

	// 리스폰도 초기 스폰과 같은 바닥-미스트리밍 경합에 노출된다 (체크포인트/프리셋 폴백 어느 경로든 동일).
	if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewShooter);
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewPartner);
	}

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
	RestorePairLoadout(ShooterPlayerState, NewShooter, NewPartner);
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

	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] DebugReloadArena PairId=%d ShooterPS=%s PartnerPS=%s"),
		PairId, *GetNameSafe(ShooterPS), *GetNameSafe(PartnerPS));

	//시작은 save 데이터 없이 start
	FTransform ShooterSpawn;
	FTransform PartnerSpawn;
	ResolveFallbackSpawnTransforms(Requester, ShooterSpawn, PartnerSpawn);

	ReloadArenaAndRespawnPair(ShooterPS, PartnerPS, ShooterSpawn, PartnerSpawn);
}

bool AOutlierGameMode::ReloadArenaAndRespawnPair(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	const FTransform& ShooterSpawn,
	const FTransform& PartnerSpawn,
	bool bRestoreCheckpointSnapshot)
{
	if (!ShooterPlayerState)
	{
		return false;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (!ArenaSubsystem || bArenaReloadInProgress)
	{
		return false;
	}
	const bool bUseGameplayDataReload = ArenaSubsystem && ArenaSubsystem->IsGameplayDataLayerAvailable();
	const uint32 ReloadGeneration = bUseGameplayDataReload
		? ArenaSubsystem->ReserveGameplayGeneration()
		: 0;
	if (bUseGameplayDataReload && ReloadGeneration == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[ReloadArenaAndRespawnPair] Gameplay reload is already in progress"));
		return false;
	}
	bPendingGameplayDataReload = bUseGameplayDataReload;

	// 2) 기존 페어 정리 (RespawnPairAtCheckpoint와 동일). 파트너가 적 빙의 중이면 먼저 해제.
	AShooterCharacter* OldShooter = ShooterPlayerState->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPlayerState->GetPartnerCharacter();
	if (!OldPartner && PartnerPlayerState)
	{
		OldPartner = PartnerPlayerState->GetPartnerCharacter();
	}

	if (APartnerPlayerController* PartnerPC = Cast<APartnerPlayerController>(GetControllerFromPlayerState(PartnerPlayerState)))
	{
		if (Cast<AEnemyBase>(PartnerPC->GetPawn()))
		{
			PartnerPC->ReleaseEnemyPossession();
		}
	}

	// 기존 Pawn은 곧 파괴되지만, 활성 Ability가 파괴 과정에서 후속 이벤트를 만들지 않도록
	// 서버에서 먼저 취소한다. 새 Pawn은 새 ASC를 가지므로 쿨다운과 일시 효과도 이어지지 않는다.
	if (OldShooter && OldShooter->GetAbilitySystemComponent())
	{
		OldShooter->GetAbilitySystemComponent()->CancelAllAbilities();
	}
	if (OldPartner && OldPartner->GetAbilitySystemComponent())
	{
		OldPartner->GetAbilitySystemComponent()->CancelAllAbilities();
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
	if (!NewShooter || !NewPartner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ReloadArenaAndRespawnPair] Failed to spawn pair Shooter=%s Partner=%s"),
			*GetNameSafe(NewShooter),
			*GetNameSafe(NewPartner));
		if (NewShooter)
		{
			NewShooter->Destroy();
		}
		if (NewPartner)
		{
			NewPartner->Destroy();
		}
		return false;
	}

	// possess는 아래에서 지오메트리 준비 뒤로 미루지만, 스폰 즉시 중력은 적용되므로
	// (possess 여부와 무관하게 CharacterMovement가 낙하시킴) 바닥 셀 준비까지 별도로 붙잡아야 한다.
	if (ArenaSubsystem)
	{
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewShooter);
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewPartner);
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, NewShooter, NewPartner);
	RestorePairLoadout(
		ShooterPlayerState,
		NewShooter,
		NewPartner,
		bRestoreCheckpointSnapshot);
	if (bRestoreCheckpointSnapshot)
	{
		if (UOutlierAbilitySystemComponent* ShooterASC = NewShooter->GetOutlierAbilitySystemComponent())
		{
			ShooterASC->RestoreHealthToMax();
		}
		if (UOutlierAbilitySystemComponent* PartnerASC = NewPartner->GetOutlierAbilitySystemComponent())
		{
			PartnerASC->RestoreHealthToMax();
		}
	}
	else if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		// 프리셋/디버그 재로드는 새 진행이다. 현재 Arena에서 파괴된 터렛을 새 Actor에 이어주지 않는다.
		SaveSubsystem->RestoreCurrentDestroyedTurretIds(TSet<FName>());
	}

	// 4) possess 배선 — 지오메트리 준비 뒤로 지연
	//    remote → 클라 스트리밍 완료 후 OnClientArenaReady에서 possess
	//    local  → 서버 Gameplay Data Layer 준비(OnArenaGameplayReady) 후 possess (아래 5)
	AController* ShooterController = GetControllerFromPlayerState(ShooterPlayerState);
	AController* PartnerController = GetControllerFromPlayerState(PartnerPlayerState);

	PendingGameplayGeneration = ReloadGeneration;
	bServerArenaReloadReady = false;
	PendingGameplayGCPlayers.Reset();
	ReadyGameplayGCPlayers.Reset();
	UE_LOG(LogTemp, Display,
		TEXT("[ReloadArenaAndRespawnPair] Generation=%u ReloadMode=%s"),
		ReloadGeneration,
		bUseGameplayDataReload ? TEXT("GameplayDataLayer") : TEXT("FullLevelInstanceFallback"));

	// 클라이언트는 ClientArenaReload를 "받고 나서야" 언로드를 시작하므로, 이 RPC를 보내기
	// 직전에 서버 쪽 가시성을 먼저 내려두면 순서가 보장된다. 그렇게 안 하면 클라가 아직
	// 레벨을 내린 구간에도 서버는 그 레벨 액터들을 계속 리플리케이트해서, 클라의
	// SerializeNewActor가 실패하고 레벨배치 액터(Rifle/StatMachine 등)의 채널이 영구히 닫힌다.

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
				// 스폰 위치를 같이 넘긴다. StageId -> 좌표 변환은 여기(ResolvePresetStageSpawn)에서만
				// 일어나고 클라에는 그 결과가 갈 창구가 없다. 안 보내면 클라의 임시 스트리밍 소스가
				// 최초 진입 때 받은 좌표에 그대로 서고, 그 자리는 이미 로드돼 있어서 새 목적지를
				// 한 번도 요청하지 않은 채 준비 완료를 보고해버린다.
				if (bUseGameplayDataReload)
				{
					PendingGameplayGCPlayers.Add(PC);
					FPC->ClientArenaGameplayReload(ReloadGeneration, ShooterSpawn.GetLocation());
				}
				else
				{
					ArenaSubsystem->SuspendArenaVisibilityForConnection(FPC);
					FPC->ClientArenaReload(ShooterSpawn.GetLocation());
				}
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
				// PC마다 자기 폰의 스폰 위치를 보낸다 (PossessMatchedPawn의 최초 진입과 동일한 규칙).
				if (bUseGameplayDataReload)
				{
					PendingGameplayGCPlayers.Add(PC);
					FPC->ClientArenaGameplayReload(ReloadGeneration, PartnerSpawn.GetLocation());
				}
				else
				{
					ArenaSubsystem->SuspendArenaVisibilityForConnection(FPC);
					FPC->ClientArenaReload(PartnerSpawn.GetLocation());
				}
			}
		}
	}

	// 5) 로컬 possess 대기 바인딩 + 서버측 리로드 시작
	if (bUseGameplayDataReload)
	{
		bArenaReloadInProgress = true;
		if (!ArenaShownHandle.IsValid())
		{
			ArenaShownHandle = ArenaSubsystem->OnArenaGameplayReady.AddUObject(
				this, &AOutlierGameMode::HandleServerArenaGameplayReady);
			ArenaReloadStalledHandle = ArenaSubsystem->OnArenaGameplayReloadStalled.AddUObject(
				this, &AOutlierGameMode::HandleArenaGameplayReloadStalled);
			ArenaReloadResumedHandle = ArenaSubsystem->OnArenaGameplayReloadResumed.AddUObject(
				this, &AOutlierGameMode::HandleArenaGameplayReloadResumed);
			ArenaReloadFailedHandle = ArenaSubsystem->OnArenaGameplayReloadFailed.AddUObject(
				this, &AOutlierGameMode::HandleArenaGameplayReloadFailed);
		}
	}
	else
	{
		bArenaReloadInProgress = true;
		if (!ArenaShownHandle.IsValid())
		{
			ArenaShownHandle = ArenaSubsystem->OnArenaShown.AddUObject(
				this, &AOutlierGameMode::HandleServerArenaShown);
		}
	}

	if (bUseGameplayDataReload)
	{
		if (!ArenaSubsystem->ReloadGameplayData(
			ReloadGeneration, PendingGameplayGCPlayers.Num() > 0))
		{
			UE_LOG(LogTemp, Error, TEXT("[ReloadArenaAndRespawnPair] Failed to start Generation=%u"), ReloadGeneration);
			if (ArenaSubsystem->GetGameplayReloadPhase() != EOutlierGameplayReloadPhase::Failed)
			{
				HandleArenaGameplayReloadFailed(
					ReloadGeneration,
					EOutlierGameplayReloadFailure::InvalidRuntime);
			}
		}
	}
	else
	{
		ArenaSubsystem->ReloadArena();
	}

	return true;
}

void AOutlierGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	const AOutlierPlayerState* State = NewPlayer
		? NewPlayer->GetPlayerState<AOutlierPlayerState>() : nullptr;
	if (bListenGuestDisconnected && State
		&& State->GetTemporaryPlayerId() == ListenGuestPlayerId)
	{
		TryResumeListenGuestAfterReconnect(NewPlayer);
		return;
	}

	if (Cast<AFrontendPlayerController>(NewPlayer))
	{
		if (UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
			: nullptr)
		{
			Identity->RegisterPlayer(NewPlayer);
		}
	}

	if (!IsArenaWorkerProcess())
	{
		return;
	}
	if (bArenaWorkerPairStarted)
	{
		TryResumeArenaWorkerAfterReconnect(NewPlayer);
		return;
	}

	if (UsesStaticArenaHandoff())
	{
		if (ArenaWorkerAdmission.IsReady()
			&& ArenaWorkerShooterController.IsValid()
			&& ArenaWorkerPartnerController.IsValid()
			&& !bArenaWorkerPairStartScheduled)
		{
			// 두 번째 PostLogin이 끝난 뒤 페어 셋업을 시작한다.
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
		// 두 번째 PostLogin이 완전히 끝난 뒤 페어 셋업이 시작되도록 다음 틱까지 지연.
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
			TEXT("[ArenaWorker] Preparing assigned pair Match=%s"),
			*ArenaWorkerAdmission.MatchId.ToString());
		StartMatchedPair(
			ShooterController,
			PartnerController,
			/*PairId=*/0,
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
		TEXT("[ArenaWorker] Starting direct-connect pair"));
	StartMatchedPair(
		ShooterController,
		PartnerController,
		/*PairId=*/0,
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
	UE_LOG(LogTemp, Display, TEXT("[ArenaReturn] Arena Worker exit requested"));
	RequestEngineExit(TEXT("Outlier Arena Worker lifecycle completed"));
}

bool AOutlierGameMode::IsArenaWorkerReconnectRequest(
	const FOutlierArenaHandoffRequest& Request) const
{
	return IsArenaWorkerProcess() && ArenaWorkerAdmission.IsReconnect(Request);
}

bool AOutlierGameMode::IsListenReconnectRequest(const FString& Options) const
{
	// 끊긴 Guest 자리만 예약한다. 토큰이 맞아도 기존 Controller가 남아 있으면 중복 접속이다.
	if (GetNetMode() != NM_ListenServer
		|| !bListenGuestDisconnected
		|| !ListenGuestReconnectToken.IsValid() || !IsValid(ListenGuestReconnectPawn.Get()))
	{
		return false;
	}
	FGuid RequestedToken;
	if (!FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("ListenReconnect")), RequestedToken)
		|| RequestedToken != ListenGuestReconnectToken)
	{
		return false;
	}
	const UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>() : nullptr;
	return !Identity || !Identity->FindPlayer(ListenGuestPlayerId);
}

void AOutlierGameMode::TryResumeListenGuestAfterReconnect(APlayerController* ReconnectedPlayer)
{
	AOutlierPlayerState* GuestState = ReconnectedPlayer
		? ReconnectedPlayer->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AOutlierPlayerState* HostState = FindPairPlayerState(ListenGuestPairId,
		ListenGuestRole == EOutlierPlayerRole::Shooter
			? EOutlierPlayerRole::Partner : EOutlierPlayerRole::Shooter);
	APawn* GuestPawn = ListenGuestReconnectPawn.Get();
	AController* HostController = GetControllerFromPlayerState(HostState);
	AFirstPersonCharacter* HostPawn = Cast<AFirstPersonCharacter>(
		HostController ? HostController->GetPawn() : nullptr);
	if (!GuestState || !HostState || !IsValid(GuestPawn))
	{
		UE_LOG(LogTemp, Error, TEXT("[ListenReconnect] Guest join deferred: Pair or Pawn unavailable"));
		return;
	}
	if (!bArenaReloadInProgress
		&& !PrepareReconnectPawn(ReconnectedPlayer, GuestPawn, HostPawn))
	{
		UE_LOG(LogTemp, Error, TEXT("[ListenReconnect] Guest join deferred: no safe Room destination"));
		return;
	}

	// 새 PlayerState와 보존한 Pawn을 다시 묶은 후에 클라이언트 스트리밍을 요청한다.
	// Reload 중에는 클라이언트 ACK 때 Room을 판정하므로 여기서 미리 이동시키지 않는다.
	GuestState->CopyReconnectGameplayStateFrom(*HostState);
	AShooterCharacter* Shooter = ListenGuestRole == EOutlierPlayerRole::Shooter
		? Cast<AShooterCharacter>(GuestPawn) : HostState->GetShooterCharacter();
	APartnerCharacter* Partner = ListenGuestRole == EOutlierPlayerRole::Partner
		? Cast<APartnerCharacter>(GuestPawn) : HostState->GetPartnerCharacter();
	RegisterSpawnedPair(ListenGuestRole == EOutlierPlayerRole::Shooter ? GuestState : HostState,
		ListenGuestRole == EOutlierPlayerRole::Partner ? GuestState : HostState, Shooter, Partner);
	PendingPossessions.Add(ReconnectedPlayer, GuestPawn);
	if (AFirstPersonPlayerController* GuestController = Cast<AFirstPersonPlayerController>(ReconnectedPlayer))
	{
		GuestController->ClientConfigureListenReconnect(ListenGuestReconnectToken);
		if (bArenaReloadInProgress)
		{
			// Reload 중 사라진 Controller를 GC 집계에서 빼고 새 연결을 등록한다.
			// Possess ACK에서는 그 시점의 Room을 다시 검사한다.
			for (auto It = PendingGameplayGCPlayers.CreateIterator(); It; ++It)
			{
				if (!It->IsValid())
				{
					It.RemoveCurrent();
				}
			}
			for (auto It = ReadyGameplayGCPlayers.CreateIterator(); It; ++It)
			{
				if (!It->IsValid())
				{
					It.RemoveCurrent();
				}
			}
			PendingReconnectContexts.Add(ReconnectedPlayer, FRoomCombatReconnectContext());
			if (bPendingGameplayDataReload)
			{
				PendingGameplayGCPlayers.Add(ReconnectedPlayer);
				GuestController->ClientArenaGameplayReload(PendingGameplayGeneration,
					GuestPawn->GetActorLocation());
			}
			else
			{
				GuestController->ClientArenaReload(GuestPawn->GetActorLocation());
			}
		}
		else
		{
			const uint32 RequestId = ++NextReconnectRequestId;
			PendingReconnectRequestIds.Add(ReconnectedPlayer, RequestId);
			GuestController->ClientArenaLoad(GuestPawn->GetActorLocation(), RequestId);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[ListenReconnect] Guest accepted; waiting for Room streaming ACK"));
}

void AOutlierGameMode::ClearListenReconnectPawn()
{
	if (IsValid(ListenGuestReconnectPawn.Get()) && !ListenGuestReconnectPawn->GetController())
	{
		ListenGuestReconnectPawn->Destroy();
	}
	ListenGuestReconnectPawn = nullptr;
	bListenGuestDisconnected = false;
	ArenaWorkerDisconnectContexts.Remove(ListenGuestPlayerId);
	ArenaWorkerReconnectDamageStates.Remove(ListenGuestPlayerId);
	ListenGuestReconnectToken.Invalidate();
	ListenGuestPlayerId.Invalidate();
}

void AOutlierGameMode::ScheduleArenaWorkerReconnectTimeout()
{
	// 두 번째 이탈이나 반복 통보로 유예를 연장하지 않는다. 기존 타이머가 있으면 최초 마감을 유지한다.
	if (bArenaWorkerMatchCompleting
		|| GetWorldTimerManager().IsTimerActive(ArenaWorkerReconnectTimerHandle))
	{
		return;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const float GraceSeconds = Settings
		? FMath::Max(Settings->ArenaWorkerReconnectGraceSeconds, 1.0f)
		: 30.0f;
	GetWorldTimerManager().SetTimer(
		ArenaWorkerReconnectTimerHandle,
		this,
		&AOutlierGameMode::HandleArenaWorkerReconnectTimeout,
		GraceSeconds,
		false);
	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaWorker] Player disconnected; waiting %.1f seconds for reconnect Match=%s"),
		GraceSeconds,
		*ArenaWorkerAdmission.MatchId.ToString());
}

void AOutlierGameMode::HandleArenaWorkerReconnectTimeout()
{
	if (ArenaWorkerShooterController.IsValid()
		&& ArenaWorkerPartnerController.IsValid()
		&& ArenaWorkerDisconnectedPlayerIds.IsEmpty()
		&& PendingReconnectContexts.IsEmpty())
	{
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("[ArenaWorker] Reconnect grace expired; releasing Match=%s"),
		*ArenaWorkerAdmission.MatchId.ToString());
	if (bArenaReloadInProgress)
	{
		if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
			: nullptr)
		{
			ArenaSubsystem->FailGameplayReload(
				PendingGameplayGeneration,
				EOutlierGameplayReloadFailure::RequiredClientDisconnected);
		}
	}
	if (!bArenaWorkerMatchCompleting)
	{
		BeginArenaWorkerReleaseShutdown();
	}
}

bool AOutlierGameMode::PrepareReconnectPawn(
	APlayerController* PlayerController, APawn* Pawn, AFirstPersonCharacter* Anchor)
{
	// 서버의 현재 차단 Room이 있으면 그 안으로 합류시킨 뒤 클라이언트 로드를 요청한다.
	// 끊긴 사이 이전 Room이 사라졌다면 옛 좌표를 쓰지 않고 Arena 시작점으로 돌린다.
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(Pawn);
	URoomCombatSubsystem* Combat = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	if (!PlayerController || !IsValid(Player) || !Combat)
	{
		return false;
	}
	FRoomCombatReconnectContext Context;
	if (Combat->GetReconnectContext(Context))
	{
		if (!Combat->TryPlaceReconnectingPlayer(Player, Anchor, Context))
		{
			return false;
		}
	}
	else if (const AOutlierPlayerState* State = PlayerController->GetPlayerState<AOutlierPlayerState>())
	{
		const FRoomCombatReconnectContext* OldContext =
			ArenaWorkerDisconnectContexts.Find(State->GetTemporaryPlayerId());
		if (OldContext && (!Combat->IsRoomRegistered(OldContext->RoomTag)
			|| Combat->GetRoomState(OldContext->RoomTag) == ERoomCombatState::Dormant)
			&& !MoveReconnectPawnToFallback(PlayerController, Player))
		{
			return false;
		}
	}
	PendingReconnectContexts.Add(PlayerController, Context);
	return true;
}

bool AOutlierGameMode::ValidateReconnectPawn(
	APlayerController* PlayerController, APawn* Pawn)
{
	// 클라이언트가 로딩하는 동안 Room이 바뀔 수 있다. 준비 때의 토큰과 서버의
	// 현재 상태를 다시 비교해, 이전 Room용 ACK로는 새 Room에 Possess하지 않는다.
	FRoomCombatReconnectContext* Previous = PendingReconnectContexts.Find(PlayerController);
	if (!Previous)
	{
		return true;
	}
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(Pawn);
	URoomCombatSubsystem* Combat = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	if (!IsValid(Player) || !Combat)
	{
		return false;
	}
	const FRoomCombatReconnectContext OldContext = *Previous;
	FRoomCombatReconnectContext Current;
	const bool bBlockedRoom = Combat->GetReconnectContext(Current);
	if (bBlockedRoom)
	{
		AOutlierPlayerState* State = PlayerController->GetPlayerState<AOutlierPlayerState>();
		AFirstPersonCharacter* Anchor = State
			? Cast<AFirstPersonCharacter>(State->IsShooterPlayer()
				? static_cast<APawn*>(State->GetPartnerCharacter())
				: static_cast<APawn*>(State->GetShooterCharacter()))
			: nullptr;
		const FVector Before = Player->GetActorLocation();
		if (!Combat->TryPlaceReconnectingPlayer(Player, Anchor, Current))
		{
			if (IsArenaWorkerProcess())
			{
				ScheduleArenaWorkerReconnectTimeout();
			}
			UE_LOG(LogTemp, Warning, TEXT("[Arena] Reconnect remains pending: no safe Room destination PC=%s"),
				*GetNameSafe(PlayerController));
			return false;
		}
		*Previous = Current;
		// 로딩 ACK가 이전 위치를 대상으로 왔다면, 새 위치의 WP 셀을 로드한 뒤 다시 ACK받는다.
		if (OldContext.RoomTag != Current.RoomTag
			|| OldContext.RoomRegistrationId != Current.RoomRegistrationId
			|| OldContext.GameplayGeneration != Current.GameplayGeneration
			|| !Before.Equals(Player->GetActorLocation()))
		{
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				const uint32 RequestId = ++NextReconnectRequestId;
				PendingReconnectRequestIds.Add(PlayerController, RequestId);
				FirstPersonController->ClientArenaLoad(Player->GetActorLocation(), RequestId);
			}
			return false;
		}
	}
	else if (OldContext.RoomTag.IsValid())
	{
		// Clear는 그대로 입장해도 되지만 Reset/언로드로 Room 자체가 사라졌다면
		// 이전 차단막 안쪽 좌표를 재사용하지 않고 Arena 시작점의 스트리밍을 다시 요청한다.
		if (!Combat->IsRoomRegistered(OldContext.RoomTag)
			|| Combat->GetRoomState(OldContext.RoomTag) == ERoomCombatState::Dormant)
		{
			if (!MoveReconnectPawnToFallback(PlayerController, Player))
			{
				if (IsArenaWorkerProcess())
				{
					ScheduleArenaWorkerReconnectTimeout();
				}
				UE_LOG(LogTemp, Warning, TEXT("[Arena] Reconnect remains pending: fallback unavailable PC=%s"),
					*GetNameSafe(PlayerController));
				return false;
			}
			*Previous = FRoomCombatReconnectContext();
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				const uint32 RequestId = ++NextReconnectRequestId;
				PendingReconnectRequestIds.Add(PlayerController, RequestId);
				FirstPersonController->ClientArenaLoad(Player->GetActorLocation(), RequestId);
			}
			return false;
		}
	}
	PendingReconnectContexts.Remove(PlayerController);
	return true;
}

bool AOutlierGameMode::MoveReconnectPawnToFallback(
	APlayerController* PlayerController, AFirstPersonCharacter* Player)
{
	if (!PlayerController || !IsValid(Player))
	{
		return false;
	}
	FTransform ShooterStart, PartnerStart;
	ResolveFallbackSpawnTransforms(PlayerController, ShooterStart, PartnerStart);
	const AOutlierPlayerState* State = PlayerController->GetPlayerState<AOutlierPlayerState>();
	const FTransform& Start = State && State->IsShooterPlayer() ? ShooterStart : PartnerStart;
	return Player->TeleportTo(Start.GetLocation(), Start.GetRotation().Rotator(), false, false);
}

void AOutlierGameMode::ClearArenaWorkerReconnectPawns()
{
	// 매치 종료 시 Controller 없는 보존 Pawn과 아직 Possess를 기다리는 Pawn을 정리한다.
	// 이미 Possess된 Pawn은 여기서 파괴하지 않는다.
	for (const TPair<TWeakObjectPtr<APlayerController>, FRoomCombatReconnectContext>& Entry
		: PendingReconnectContexts)
	{
		if (TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(Entry.Key.Get()))
		{
			if (IsValid(PendingPawn->Get()) && !(*PendingPawn)->GetController())
			{
				(*PendingPawn)->Destroy();
			}
			PendingPossessions.Remove(Entry.Key.Get());
		}
	}
	for (const TPair<FGuid, TObjectPtr<APawn>>& Entry : ArenaWorkerReconnectPawns)
	{
		if (IsValid(Entry.Value.Get()) && !Entry.Value->GetController())
		{
			Entry.Value->Destroy();
		}
	}
	ArenaWorkerReconnectPawns.Reset();
	ArenaWorkerReconnectDamageStates.Reset();
	PendingReconnectContexts.Reset();
	ArenaWorkerDisconnectContexts.Reset();
	PendingReconnectRequestIds.Reset();
	ArenaWorkerReconnectGameplayState = FOutlierReconnectGameplayState();
	bHasArenaWorkerReconnectGameplayState = false;
}

void AOutlierGameMode::TryResumeArenaWorkerAfterReconnect(APlayerController* ReconnectedPlayer)
{
	// 둘 다 끊긴 경우 첫 번째 접속만으로 게임을 재개하지 않는다. 두 Role의 Controller가
	// 모두 복원된 시점에 Pair 링크와 Pawn을 한 번에 다시 구성한다.
	if (!ReconnectedPlayer
		|| !ArenaWorkerAdmission.bPairStarted
		|| !ArenaWorkerShooterController.IsValid()
		|| !ArenaWorkerPartnerController.IsValid())
	{
		return;
	}

	AOutlierPlayerState* ReconnectedPlayerState = ReconnectedPlayer->GetPlayerState<AOutlierPlayerState>();
	if (ReconnectedPlayerState)
	{
		// 한 명만 끊겼으면 남은 PS가 원본이고, 둘 다 끊겼으면 서버 스냅샷이 원본이다.
		// 새로 생성된 PS에서 진행 정보를 복사하면 두 번째 재접속자의 정보가 초기화된다.
		if (ArenaWorkerDisconnectedPlayerIds.Num() == 2
			&& bHasArenaWorkerReconnectGameplayState)
		{
			ReconnectedPlayerState->RestoreReconnectGameplayState(ArenaWorkerReconnectGameplayState);
			if (AOutlierPlayerState* OtherState = FindPairPlayerState(
				ReconnectedPlayerState->GetPairId(),
				ReconnectedPlayerState->IsShooterPlayer()
					? EOutlierPlayerRole::Partner : EOutlierPlayerRole::Shooter))
			{
				OtherState->RestoreReconnectGameplayState(ArenaWorkerReconnectGameplayState);
			}
		}
		else if (AOutlierPlayerState* RemainingPlayerState = FindPairPlayerState(
			ReconnectedPlayerState->GetPairId(),
			ReconnectedPlayerState->IsShooterPlayer()
				? EOutlierPlayerRole::Partner : EOutlierPlayerRole::Shooter);
			RemainingPlayerState && RemainingPlayerState != ReconnectedPlayerState)
		{
			ReconnectedPlayerState->CopyReconnectGameplayStateFrom(*RemainingPlayerState);
		}
		else if (bHasArenaWorkerReconnectGameplayState)
		{
			ReconnectedPlayerState->RestoreReconnectGameplayState(ArenaWorkerReconnectGameplayState);
		}
	}
	RefreshPairLinks(ReconnectedPlayerState);

	auto ResolveReconnectedController = [this](const FGuid& PlayerId) -> APlayerController*
	{
		if (PlayerId == ArenaWorkerAdmission.ShooterPlayerId)
		{
			return ArenaWorkerShooterController.Get();
		}
		if (PlayerId == ArenaWorkerAdmission.PartnerPlayerId)
		{
			return ArenaWorkerPartnerController.Get();
		}
		return nullptr;
	};

	if (bArenaReloadInProgress)
	{
		// 끊긴 PC의 Weak Pointer는 절대 ACK하지 않는다. 무효 항목을 제거하고 원래 PlayerId에
		// 해당하는 새 PC만 같은 Generation의 대기 집합에 다시 넣어 안전 검사를 이어간다.
		for (auto It = PendingGameplayGCPlayers.CreateIterator(); It; ++It)
		{
			if (!It->IsValid())
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = ReadyGameplayGCPlayers.CreateIterator(); It; ++It)
		{
			if (!It->IsValid())
			{
				It.RemoveCurrent();
			}
		}

		for (const FGuid& PlayerId : ArenaWorkerDisconnectedPlayerIds)
		{
			APlayerController* PlayerController = ResolveReconnectedController(PlayerId);
			if (!PlayerController)
			{
				continue;
			}
			PendingGameplayGCPlayers.Add(PlayerController);
			APawn* PendingPawn = ArenaWorkerReconnectPawns.FindRef(PlayerId);
			if (PendingPawn)
			{
				PendingPossessions.Add(PlayerController, PendingPawn);
			}
			const FVector SpawnLocation = PendingPawn
				? PendingPawn->GetActorLocation()
				: PlayerController->GetSpawnLocation();
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				FirstPersonController->ClientArenaGameplayReload(
					PendingGameplayGeneration,
					SpawnLocation);
			}
		}
		if (UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
			: nullptr;
			ArenaSubsystem && ArenaSubsystem->IsGameplayReloadStalled(PendingGameplayGeneration))
		{
			ArenaSubsystem->RetryStalledGameplayReload(PendingGameplayGeneration);
		}
	}
	else
	{
		// 정상 재접속에서는 남아 있는 플레이어와 Room/Wave를 그대로 둔다.
		// 먼저 모든 보존 Pawn의 안전 위치를 확정하고, 그 뒤 Pair 링크와 클라이언트
		// 스트리밍 요청을 등록한다. 위치를 못 찾으면 누구도 부분적으로 Possess하지 않는다.
		for (const FGuid& PlayerId : ArenaWorkerDisconnectedPlayerIds)
		{
			APlayerController* PlayerController = ResolveReconnectedController(PlayerId);
			APawn* ReconnectedPawn = ArenaWorkerReconnectPawns.FindRef(PlayerId);
			const FGuid OtherPlayerId = PlayerId == ArenaWorkerAdmission.ShooterPlayerId
				? ArenaWorkerAdmission.PartnerPlayerId : ArenaWorkerAdmission.ShooterPlayerId;
			APlayerController* OtherController = ResolveReconnectedController(OtherPlayerId);
			AFirstPersonCharacter* Anchor = Cast<AFirstPersonCharacter>(
				OtherController ? OtherController->GetPawn()
					: ArenaWorkerReconnectPawns.FindRef(OtherPlayerId));
			if (!PrepareReconnectPawn(PlayerController, ReconnectedPawn, Anchor))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ArenaWorker] Reconnect deferred: Pawn or safe Room destination unavailable Player=%s"),
					*PlayerId.ToString());
				return;
			}
		}
		// 두 명 모두 끊긴 동안에는 기존 PlayerState의 Pair 포인터도 사라진다.
		// 스트리밍 ACK보다 먼저 보존한 Pawn과 새 PlayerState를 다시 묶는다.
		AOutlierPlayerState* ShooterState = ArenaWorkerShooterController->GetPlayerState<AOutlierPlayerState>();
		AOutlierPlayerState* PartnerState = ArenaWorkerPartnerController->GetPlayerState<AOutlierPlayerState>();
		AShooterCharacter* Shooter = Cast<AShooterCharacter>(ArenaWorkerShooterController->GetPawn());
		APartnerCharacter* Partner = Cast<APartnerCharacter>(ArenaWorkerPartnerController->GetPawn());
		if (!Shooter)
		{
			Shooter = Cast<AShooterCharacter>(ArenaWorkerReconnectPawns.FindRef(ArenaWorkerAdmission.ShooterPlayerId));
		}
		if (!Partner)
		{
			Partner = Cast<APartnerCharacter>(ArenaWorkerReconnectPawns.FindRef(ArenaWorkerAdmission.PartnerPlayerId));
		}
		RegisterSpawnedPair(ShooterState, PartnerState, Shooter, Partner);
		for (const FGuid& PlayerId : ArenaWorkerDisconnectedPlayerIds)
		{
			APlayerController* PlayerController = ResolveReconnectedController(PlayerId);
			APawn* ReconnectedPawn = ArenaWorkerReconnectPawns.FindRef(PlayerId);
			const FVector SpawnLocation = ReconnectedPawn->GetActorLocation();
			PendingPossessions.Add(PlayerController, ReconnectedPawn);
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				const uint32 RequestId = ++NextReconnectRequestId;
				PendingReconnectRequestIds.Add(PlayerController, RequestId);
				FirstPersonController->ClientArenaLoad(SpawnLocation, RequestId);
			}
		}
	}
	if (bArenaReloadInProgress)
	{
		GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	}

	for (const FGuid& PlayerId : ArenaWorkerDisconnectedPlayerIds)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaWorker] Player reconnected Match=%s Player=%s"),
			*ArenaWorkerAdmission.MatchId.ToString(),
			*PlayerId.ToString());
	}
	ArenaWorkerDisconnectedPlayerIds.Reset();
	ArenaWorkerReconnectPawns.Reset();
	TryFinishArenaReload();
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

void AOutlierGameMode::HandleServerArenaShown()
{
	if (!bArenaReloadInProgress)
	{
		return;
	}
	CompleteServerArenaReload();
}

void AOutlierGameMode::HandleServerArenaGameplayReady(uint32 GameplayGeneration)
{
	if (!bArenaReloadInProgress || GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}
	CompleteServerArenaReload();
}

void AOutlierGameMode::HandleArenaGameplayReloadStalled(
	uint32 GameplayGeneration,
	EOutlierGameplayReloadPhase Phase,
	FString Diagnostic)
{
	if (!bArenaReloadInProgress || GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("[Arena][DataLayer] Reload remains stalled without automatic recovery: %s"),
		*Diagnostic);
	if (!IsArenaWorkerProcess())
	{
		return;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const float StallSeconds = Settings
		? FMath::Max(Settings->ArenaGameplayReloadStallSeconds, 1.0f)
		: 15.0f;
	const float FailureSeconds = Settings
		? FMath::Max(Settings->ArenaGameplayReloadFailureSeconds, StallSeconds)
		: 60.0f;
	const float RemainingSeconds = FMath::Max(FailureSeconds - StallSeconds, 0.1f);
	GetWorldTimerManager().SetTimer(
		ArenaWorkerReloadFailureTimerHandle,
		this,
		&AOutlierGameMode::HandleArenaWorkerReloadStallTimeout,
		RemainingSeconds,
		false);

	UE_LOG(LogTemp, Error,
		TEXT("[ArenaWorker][DataLayer] Generation=%u Phase=%s will fail after %.1f additional seconds"),
		GameplayGeneration,
		*UEnum::GetValueAsString(Phase),
		RemainingSeconds);
}

void AOutlierGameMode::HandleArenaGameplayReloadResumed(uint32 GameplayGeneration)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ArenaWorkerReloadFailureTimerHandle);
}

void AOutlierGameMode::HandleArenaGameplayReloadFailed(
	uint32 GameplayGeneration,
	EOutlierGameplayReloadFailure Failure)
{
	if (GameplayGeneration != PendingGameplayGeneration)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(ArenaWorkerReloadFailureTimerHandle);
	ClearPendingArenaReloadPawns();
	PendingGameplayGCPlayers.Reset();
	ReadyGameplayGCPlayers.Reset();
	bArenaReloadInProgress = false;
	bPendingGameplayDataReload = false;
	bServerArenaReloadReady = false;

	const FString Diagnostic = FString::Printf(
		TEXT("Gameplay reload failed. Generation=%u Failure=%s"),
		GameplayGeneration,
		*UEnum::GetValueAsString(Failure));
	if (IsArenaWorkerProcess())
	{
		BeginArenaWorkerReleaseShutdown();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Arena][DataLayer] %s"), *Diagnostic);
	}
	ClearArenaGameplayReloadDelegates();
}

void AOutlierGameMode::HandleArenaWorkerReloadStallTimeout()
{
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (ArenaSubsystem && ArenaSubsystem->IsGameplayReloadStalled(PendingGameplayGeneration))
	{
		ArenaSubsystem->FailGameplayReload(
			PendingGameplayGeneration,
			EOutlierGameplayReloadFailure::WorkerStallTimeout);
	}
}

void AOutlierGameMode::BeginArenaWorkerReleaseShutdown()
{
	if (bArenaWorkerMatchCompleting)
	{
		return;
	}

	// 정상 Match 완료와 복구 불가능한 Reload/재접속 실패는 같은 종료 계약을 사용한다.
	// 제어 채널에 Releasing을 먼저 알리고 플레이어를 Lobby로 보낸 뒤 Worker를 종료한다.
	bArenaWorkerMatchCompleting = true;
	GetWorldTimerManager().ClearTimer(ArenaWorkerAutoCompleteTimerHandle);
	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	ArenaWorkerDisconnectedPlayerIds.Reset();
	ClearArenaWorkerReconnectPawns();
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr)
	{
		ProcessSubsystem->NotifyWorkerReleasing(ArenaWorkerAdmission.MatchId);
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const FString LobbyAddress = Settings
		? Settings->ResolveLobbyAddress()
		: FString();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PlayerController = It->Get())
		{
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				// 의도된 Worker 종료를 다음 NetworkFailure가 장애로 되돌리지 않도록
				// 두 클라이언트 모두 Travel 전에 로컬 재접속 상태를 먼저 정리한다.
				FirstPersonController->ClientPrepareForArenaExit();
			}
			if (Settings && Settings->bReturnToLobbyOnMatchEnd && !LobbyAddress.IsEmpty())
			{
				PlayerController->ClientTravel(LobbyAddress, TRAVEL_Absolute);
			}
		}
	}

	const float ExitDelay = Settings
		? FMath::Max(Settings->ArenaWorkerExitTimeoutSeconds, 0.1f)
		: 5.0f;
	GetWorldTimerManager().SetTimer(
		ArenaWorkerExitTimerHandle,
		this,
		&AOutlierGameMode::RequestArenaWorkerExit,
		ExitDelay,
		false);
}

void AOutlierGameMode::ClearArenaGameplayReloadDelegates()
{
	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (ArenaSubsystem)
	{
		ArenaSubsystem->OnArenaGameplayReady.Remove(ArenaShownHandle);
		ArenaSubsystem->OnArenaShown.Remove(ArenaShownHandle);
		ArenaSubsystem->OnArenaGameplayReloadStalled.Remove(ArenaReloadStalledHandle);
		ArenaSubsystem->OnArenaGameplayReloadResumed.Remove(ArenaReloadResumedHandle);
		ArenaSubsystem->OnArenaGameplayReloadFailed.Remove(ArenaReloadFailedHandle);
	}
	ArenaShownHandle.Reset();
	ArenaReloadStalledHandle.Reset();
	ArenaReloadResumedHandle.Reset();
	ArenaReloadFailedHandle.Reset();
}

void AOutlierGameMode::ClearPendingArenaReloadPawns()
{
	TSet<TObjectPtr<APawn>> PendingPawns;
	for (const TPair<TObjectPtr<APlayerController>, TObjectPtr<APawn>>& Pair : PendingPossessions)
	{
		PendingPawns.Add(Pair.Value);
	}
	for (const TPair<TObjectPtr<APlayerController>, TObjectPtr<APawn>>& Pair : PendingLocalPossessions)
	{
		PendingPawns.Add(Pair.Value);
	}
	for (APawn* Pawn : PendingPawns)
	{
		if (Pawn)
		{
			Pawn->Destroy();
		}
	}
	PendingPossessions.Reset();
	PendingLocalPossessions.Reset();
}

void AOutlierGameMode::CompleteServerArenaReload()
{
	// Listen Host의 로컬 Pawn은 서버 준비 완료에서 Possess한다. 원격 클라이언트의 준비 통보와 분리한다.
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
	bServerArenaReloadReady = true;
	TryFinishArenaReload();
}

void AOutlierGameMode::TryFinishArenaReload()
{
	// 서버 로딩만 끝났다고 재시작을 닫지 않는다. Possess 대기와 재접속 대기까지 해소되어야 완료한다.
	if (!bArenaReloadInProgress
		|| !bServerArenaReloadReady
		|| !PendingLocalPossessions.IsEmpty()
		|| !PendingPossessions.IsEmpty()
		|| bListenGuestDisconnected
		|| !ArenaWorkerDisconnectedPlayerIds.IsEmpty()
		|| !ArenaWorkerReconnectPawns.IsEmpty())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(ArenaWorkerReloadFailureTimerHandle);
	ClearArenaGameplayReloadDelegates();
	bArenaReloadInProgress = false;
	bServerArenaReloadReady = false;
	bPendingGameplayDataReload = false;
	FinishCheckpointRestart();
}

bool AOutlierGameMode::ResolveCheckpointTransform(AController* Controller, FTransform& OutTransform) const
{
	const AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (!PS || !PS->GetCheckpointData().IsValid())
	{
		return false;
	}

	const FOutlierCheckpointData& Data = PS->GetCheckpointData();

	const UWorld* World = GetWorld();
	const UOutlierArenaSubsystem* ArenaSubsystem = World ? World->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!ArenaSubsystem || !ArenaSubsystem->GetArenaLoadedLevel())
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

		// WP 아레나에서는 체크포인트가 아레나 PersistentLevel이 아니라 WP 셀 레벨로 들어가므로
		// GetLevel() 기반 비교가 항상 실패한다 (PlayerStart/Enemy와 동일한 사정).
		if (!ArenaSubsystem->IsActorOwnedByArena(Checkpoint))
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

void AOutlierGameMode::RestorePairLoadout(
	AOutlierPlayerState* ShooterPlayerState,
	AShooterCharacter* Shooter,
	APartnerCharacter* Partner,
	bool bRestoreCheckpointAmmo)
{
	if (!ShooterPlayerState)
	{
		return;
	}

	// 슈트를 무기보다 먼저 입힌다. 메시를 갈아끼우면 소켓이 바뀌므로,
	// 순서가 반대면 이미 붙은 무기들이 옛 메시에 매달린 채로 남는다.
	if (Shooter && ShooterPlayerState->GetAcquiredSuit())
	{
		Shooter->ApplySuitMeshes(
			ShooterPlayerState->GetSuitFirstPersonMesh(),
			ShooterPlayerState->GetSuitThirdPersonMesh());
	}

	// 값으로 복사한다. 아래 RestoreLoadout 이 슬롯을 채우면서 PlayerState 의 스냅샷을
	// 다시 쓰기 때문에, 참조를 들고 있으면 Partner 분기에서 재할당된 메모리를 읽게 된다.
	const FOutlierLoadoutSnapshot Snapshot = ShooterPlayerState->GetLoadoutSnapshot();
	if (Snapshot.IsEmpty())
	{
		// 최초 스폰에는 기록이 없다. 무기 복원만 건너뛴다 (슈트는 위에서 이미 처리).
		return;
	}

	if (UShooterInventoryComponent* Inventory = Shooter ? Shooter->GetInventoryComponent() : nullptr)
	{
		Inventory->RestoreLoadout(Snapshot, bRestoreCheckpointAmmo);
	}

	// Partner 는 InventoryComponent 가 없고 무기도 하나뿐이라 여기서 직접 처리한다.
	// 순서는 ASuitInteraction::ApplySuit 의 Partner 지급과 같다 (스폰 -> 장착 -> 표시).
	if (Partner && Snapshot.PartnerWeaponClass)
	{
		if (AWeaponBase* PartnerWeapon = AWeaponBase::SpawnLoadoutWeapon(
				GetWorld(), Snapshot.PartnerWeaponClass, Partner))
		{
			Partner->EquipWeapon(PartnerWeapon);
			PartnerWeapon->ShowEquippedPresentation();
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RestorePairLoadout] Partner weapon spawn failed Class=%s"),
				*GetNameSafe(Snapshot.PartnerWeaponClass.Get()));
		}
	}
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

bool AOutlierGameMode::BuildPairCheckpointSnapshot(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	FName CheckpointId,
	bool bInitialSnapshot,
	const FTransform& ShooterSpawn,
	const FTransform& PartnerSpawn,
	FOutlierCheckpointSnapshot& OutSnapshot) const
{
	if (!ShooterPlayerState || !PartnerPlayerState
		|| (!bInitialSnapshot && CheckpointId.IsNone()))
	{
		return false;
	}

	OutSnapshot = FOutlierCheckpointSnapshot();
	OutSnapshot.CheckpointId = CheckpointId;
	OutSnapshot.bInitialSnapshot = bInitialSnapshot;
	OutSnapshot.ShooterSpawnTransform = ShooterSpawn;
	OutSnapshot.PartnerSpawnTransform = PartnerSpawn;
	OutSnapshot.ShooterProgress.NodeCount = ShooterPlayerState->GetNodeCount();
	OutSnapshot.ShooterProgress.ActivatedUpgradeNodeIds =
		ShooterPlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter);
	// PlayerState의 일반 로드아웃 기록은 프리셋 리로드용이라 탄약을 일부러 갖지 않는다.
	// 체크포인트만 저장 순간의 살아 있는 Weapon Actor를 읽어 정확한 탄약을 남긴다.
	OutSnapshot.LoadoutSnapshot = ShooterPlayerState->GetLoadoutSnapshot();
	const AShooterCharacter* Shooter = ShooterPlayerState->GetShooterCharacter();
	const UShooterInventoryComponent* Inventory = Shooter
		? Shooter->GetInventoryComponent()
		: nullptr;
	if (!Inventory)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Checkpoint] Cannot capture live loadout without Shooter inventory PS=%s"),
			*GetNameSafe(ShooterPlayerState));
		return false;
	}
	Inventory->BuildLoadoutSnapshot(
		OutSnapshot.LoadoutSnapshot,
		/*bCaptureAmmo=*/true);
	OutSnapshot.SuitSnapshot.bAcquired = ShooterPlayerState->GetAcquiredSuit();
	OutSnapshot.SuitSnapshot.FirstPersonMesh = ShooterPlayerState->GetSuitFirstPersonMesh();
	OutSnapshot.SuitSnapshot.ThirdPersonMesh = ShooterPlayerState->GetSuitThirdPersonMesh();

	OutSnapshot.PartnerProgress.NodeCount = PartnerPlayerState->GetNodeCount();
	OutSnapshot.PartnerProgress.ActivatedUpgradeNodeIds =
		PartnerPlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Partner);

	if (const UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		OutSnapshot.WorldProgress = SaveSubsystem->GetCurrentWorldProgress();
		OutSnapshot.DestroyedTurretIds = SaveSubsystem->GetCurrentDestroyedTurretIds();
	}
	if (const UEnemyAdaptationSubsystem* EnemyAdaptationSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyAdaptationSubsystem>()
		: nullptr)
	{
		OutSnapshot.GunAdaptationStack =
			EnemyAdaptationSubsystem->GetCurrentGunAdaptationStack();
	}

	return OutSnapshot.IsValid();
}

void AOutlierGameMode::CaptureInitialCheckpointSnapshot(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	AShooterCharacter* Shooter,
	APartnerCharacter* Partner)
{
	UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	if (!SaveSubsystem || SaveSubsystem->HasInitialSnapshot() || !Shooter || !Partner)
	{
		return;
	}

	FOutlierCheckpointSnapshot Snapshot;
	if (BuildPairCheckpointSnapshot(
		ShooterPlayerState,
		PartnerPlayerState,
		NAME_None,
		true,
		Shooter->GetActorTransform(),
		Partner->GetActorTransform(),
		Snapshot))
	{
		SaveSubsystem->CaptureInitialSnapshot(Snapshot);
	}
}

void AOutlierGameMode::ResolveFallbackSpawnTransforms(
	AController* Requester,
	FTransform& OutShooterSpawn,
	FTransform& OutPartnerSpawn)
{
	// 1순위: 이 아레나 소속의 일반 PlayerStart (APresetPlayerStart는 제외된다)
	if (ResolveArenaSpawnTransforms(OutShooterSpawn, OutPartnerSpawn))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SpawnFallback] Using arena PlayerStart Shooter=%s"),
			*OutShooterSpawn.GetLocation().ToString());
		return;
	}

	// 2순위: 엔진 기본 탐색.
	AActor* FallbackStart = FindPlayerStart(Requester);
	OutShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
	OutPartnerSpawn = OutShooterSpawn;
	OutPartnerSpawn.AddToTranslation(OutShooterSpawn.GetRotation().GetRightVector() * 150.0f);

	UE_LOG(LogTemp, Warning,
		TEXT("[SpawnFallback] No arena PlayerStart; using %s at %s"),
		FallbackStart ? *GetNameSafe(FallbackStart) : TEXT("world origin"),
		*OutShooterSpawn.GetLocation().ToString());
}

bool AOutlierGameMode::ResolveArenaSpawnTransforms(FTransform& OutShooterSpawn, FTransform& OutPartnerSpawn) const
{
	const UWorld* World = GetWorld();
	const UOutlierArenaSubsystem* ArenaSubsystem = World
		? World->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaSubsystem ? ArenaSubsystem->GetArenaLoadedLevel() : nullptr;

	if (!ArenaLevel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ArenaLevel"));
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

		// APresetPlayerStart는 APlayerStart 서브클래스라 GetAllActorsOfClass에 같이 잡힌다.
		// 그리드/지정 스테이지 선택용으로만 쓰이는 위치라 일반 아레나 시작점 후보에서 제외해야 한다.
		if (PlayerStart->IsA<APresetPlayerStart>())
		{
			continue;
		}

		// WP 아레나에서는 PlayerStart가 아레나 PersistentLevel이 아니라 WP 셀 레벨로 들어가므로
		// 레벨 기반 비교로는 절대 잡히지 않는다. 소유 아레나로 매칭한다.
		if (!ArenaSubsystem->IsActorOwnedByArena(PlayerStart))
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
			TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ShooterStart ArenaStartCount=%d"),
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
