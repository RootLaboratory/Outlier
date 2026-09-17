// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Weapon/WeaponBase.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpoint.h"
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
#include "Streaming/ServerStreamingLevelsVisibility.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

AOutlierGameMode::AOutlierGameMode()
{

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
	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	ArenaWorkerDisconnectedPlayerIds.Reset();
	ArenaWorkerReconnectPawns.Reset();
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
		FirstPersonController->ClientArenaLoad(SpawnLocation);
	}
}



void AOutlierGameMode::OnClientArenaReady(APlayerController* PC)
{
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
	PendingPossessions.Remove(PC);

	PC->Possess(Pawn);
}

void AOutlierGameMode::OnClientArenaGameplayGCReady(APlayerController* PC, uint32 GameplayGeneration)
{
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

	if (bArenaReloadInProgress && !bWaitForArenaWorkerReconnect)
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
		ArenaWorkerDisconnectedPlayerIds.Add(ExitingArenaPlayerId);
		if (TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(ExitingPlayer))
		{
			// 리로드 중 Pawn은 아직 Possess되지 않아 Controller Map에만 매달려 있다.
			// 이전 Controller가 파괴되기 전에 PlayerId 키로 옮겨둬야 재접속 PC에 다시 연결할 수 있다.
			ArenaWorkerReconnectPawns.Add(ExitingArenaPlayerId, *PendingPawn);
			PendingPossessions.Remove(ExitingPlayer);
		}
		PendingLocalPossessions.Remove(ExitingPlayer);
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
		// Super::Logout이 연결을 정리하기 전에 호출해야 남아 있는 Remote PC에도
		// ReturnToMainMenu RPC를 보내고 Host 자신도 같은 흐름으로 Title에 돌아갈 수 있다.
		ReturnToMainMenuHost();
	}

	Super::Logout(Exiting);

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

void AOutlierGameMode::ReloadArenaAndRespawnPair(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	const FTransform& ShooterSpawn,
	const FTransform& PartnerSpawn)
{
	if (!ShooterPlayerState)
	{
		return;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	const bool bUseGameplayDataReload = ArenaSubsystem && ArenaSubsystem->IsGameplayDataLayerAvailable();
	const uint32 ReloadGeneration = bUseGameplayDataReload
		? ArenaSubsystem->ReserveGameplayGeneration()
		: 0;
	if (bUseGameplayDataReload && ReloadGeneration == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[ReloadArenaAndRespawnPair] Gameplay reload is already in progress"));
		return;
	}

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

	// possess는 아래에서 지오메트리 준비 뒤로 미루지만, 스폰 즉시 중력은 적용되므로
	// (possess 여부와 무관하게 CharacterMovement가 낙하시킴) 바닥 셀 준비까지 별도로 붙잡아야 한다.
	if (ArenaSubsystem)
	{
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewShooter);
		ArenaSubsystem->HoldCharacterUntilArenaCellReady(NewPartner);
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, NewShooter, NewPartner);
	RestorePairLoadout(ShooterPlayerState, NewShooter, NewPartner);

	// 4) possess 배선 — 지오메트리 준비 뒤로 지연
	//    remote → 클라 스트리밍 완료 후 OnClientArenaReady에서 possess
	//    local  → 서버 Gameplay Data Layer 준비(OnArenaGameplayReady) 후 possess (아래 5)
	AController* ShooterController = GetControllerFromPlayerState(ShooterPlayerState);
	AController* PartnerController = GetControllerFromPlayerState(PartnerPlayerState);

	PendingGameplayGeneration = ReloadGeneration;
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
	if (!ArenaSubsystem)
	{
		return;
	}

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
	else if (PendingLocalPossessions.Num() > 0)
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

void AOutlierGameMode::ScheduleArenaWorkerReconnectTimeout()
{
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
		&& ArenaWorkerPartnerController.IsValid())
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

	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	AOutlierPlayerState* ReconnectedPlayerState = ReconnectedPlayer->GetPlayerState<AOutlierPlayerState>();
	if (ReconnectedPlayerState)
	{
		AOutlierPlayerState* RemainingPlayerState = FindPairPlayerState(
			ReconnectedPlayerState->GetPairId(),
			ReconnectedPlayerState->IsShooterPlayer()
				? EOutlierPlayerRole::Partner
				: EOutlierPlayerRole::Shooter);
		if (RemainingPlayerState && RemainingPlayerState != ReconnectedPlayerState)
		{
			ReconnectedPlayerState->CopyReconnectGameplayStateFrom(*RemainingPlayerState);
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
		// Worker는 HandleStartingNewPlayer에서 기본 Spawn을 막으므로 재접속 PC에는 Pawn이 없다.
		// 기존 체크포인트 복원 경로로 Pair를 다시 만든 뒤 클라이언트 스트리밍 준비를 재요청한다.
		RespawnPairAtCheckpoint(ReconnectedPlayer);
		for (const FGuid& PlayerId : ArenaWorkerDisconnectedPlayerIds)
		{
			APlayerController* PlayerController = ResolveReconnectedController(PlayerId);
			APawn* ReconnectedPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
			if (!PlayerController || !ReconnectedPawn)
			{
				continue;
			}
			const FVector SpawnLocation = ReconnectedPawn->GetActorLocation();
			PlayerController->UnPossess();
			PendingPossessions.Add(PlayerController, ReconnectedPawn);
			if (AFirstPersonPlayerController* FirstPersonController =
				Cast<AFirstPersonPlayerController>(PlayerController))
			{
				FirstPersonController->ClientArenaLoad(SpawnLocation);
			}
		}
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
	GetWorldTimerManager().ClearTimer(ArenaWorkerReconnectTimerHandle);
	ArenaWorkerDisconnectedPlayerIds.Reset();
	ArenaWorkerReconnectPawns.Reset();
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr)
	{
		ProcessSubsystem->NotifyWorkerReleasing(ArenaWorkerAdmission.MatchId);
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const FString LobbyAddress = Settings
		? Settings->LobbyAddress.TrimStartAndEnd()
		: FString();
	if (Settings && Settings->bReturnToLobbyOnMatchEnd && !LobbyAddress.IsEmpty())
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PlayerController = It->Get())
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

	GetWorldTimerManager().ClearTimer(ArenaWorkerReloadFailureTimerHandle);
	ClearArenaGameplayReloadDelegates();
	bArenaReloadInProgress = false;
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
	APartnerCharacter* Partner)
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
		Inventory->RestoreLoadout(Snapshot);
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
