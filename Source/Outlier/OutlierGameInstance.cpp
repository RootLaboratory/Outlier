// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierGameInstance.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameMode.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Network/OutlierArenaProcessSubsystem.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "UI/LoadingWidget.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"

void UOutlierGameInstance::Init()
{
	Super::Init();

	// GAS 전역 초기화. GameplayCue 매니저를 만들고 GameplayCueNotifyPaths( DefaultGame.ini )를
	// 스캔해 Notify 클래스를 로드하는 지점이다. 이 호출이 없으면 매니저가 아무 데서나 지연 생성되고,
	// 그 타이밍에 따라 큐 노티파이 로드가 통째로 누락된다 ( 매핑표에는 있는데 클래스는 0개인 상태 ).
	// Attribute / Effect / Ability 는 이것 없이도 돌기 때문에 GameplayCue 를 붙이기 전까지는 드러나지 않는다.
	UAbilitySystemGlobals::Get().InitGlobalData();

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UOutlierGameInstance::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UOutlierGameInstance::HandlePostLoadMap);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this,
			&UOutlierGameInstance::HandleNetworkFailure);
	}
}

void UOutlierGameInstance::Shutdown()
{
	if (ArenaWorkerBootstrapTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaWorkerBootstrapTickerHandle);
		ArenaWorkerBootstrapTickerHandle.Reset();
	}

	if (GEngine && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}

	Super::Shutdown();
}

void UOutlierGameInstance::NotifyArenaHandoffStarted()
{
	bArenaHandoffActive = true;
	bLobbyRecoveryQueued = false;
	bLobbyRecoveryAttempted = false;
}

void UOutlierGameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld)
	{
		return;
	}

	if (LoadedWorld->GetNetMode() == NM_DedicatedServer)
	{
		TryBootstrapArenaWorker(LoadedWorld);
		return;
	}

	if (bLobbyRecoveryQueued)
	{
		bLobbyRecoveryQueued = false;
		TravelToLobby(LoadedWorld);
		return;
	}

	if (bArenaHandoffActive)
	{
		const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
		if (!Settings || !Settings->IsArenaWorld(LoadedWorld))
		{
			ResetArenaHandoffState();
		}
	}

	if (bTriedConnect)
	{
		return;
	}

	FString ConnectAddress;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Connect="), ConnectAddress))
	{
		return;
	}

	if (ConnectAddress.IsEmpty())
	{
		return;
	}

	bTriedConnect = true;

	if (APlayerController* PC = LoadedWorld->GetFirstPlayerController())
	{
		PC->ClientTravel(ConnectAddress, TRAVEL_Absolute);
	}
}

void UOutlierGameInstance::HandleNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	(void)World;
	(void)NetDriver;

	if (!TryQueueLobbyRecovery())
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaReturn] Arena connection failed. Lobby recovery queued Type=%s Error=%s"),
		ENetworkFailure::ToString(FailureType),
		*ErrorString);
}

void UOutlierGameInstance::TryBootstrapArenaWorker(UWorld* LoadedWorld)
{
	if (!LoadedWorld
		|| !FParse::Param(FCommandLine::Get(), TEXT("OutlierArenaWorker")))
	{
		return;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!Settings || Settings->ArenaLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaWorker] ArenaLevel is not configured"));
		return;
	}

	if (Settings->IsArenaWorld(LoadedWorld))
	{
		ArenaWorkerBootstrapWorld = LoadedWorld;
		if (!ArenaWorkerBootstrapTickerHandle.IsValid())
		{
			ArenaWorkerReadyStableFrames = 0;
			ArenaWorkerBootstrapTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &UOutlierGameInstance::HandleArenaWorkerBootstrapTick));
			UE_LOG(LogTemp, Display,
				TEXT("[ArenaWorker] Waiting for Start streaming source, WP cells and LevelInstances before Ready"));
		}
		return;
	}

	if (bArenaWorkerTravelRequested)
	{
		return;
	}

	// ServerDefaultMap으로 시작할 수 있으므로 Arena 할당 전에 최초 한 번만 설정된 맵으로 이동.
	bArenaWorkerTravelRequested = true;
	const FString ArenaPackageName = Settings->GetArenaPackageName();
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaWorker] Traveling to configured arena map %s"),
		*ArenaPackageName);
	LoadedWorld->ServerTravel(ArenaPackageName, true);
}

bool UOutlierGameInstance::HandleArenaWorkerBootstrapTick(float DeltaTime)
{
	constexpr int32 RequiredReadyStableFrames = 3;

	(void)DeltaTime;
	UWorld* World = ArenaWorkerBootstrapWorld.Get();
	if (!World || !FParse::Param(FCommandLine::Get(), TEXT("OutlierArenaWorker")))
	{
		ArenaWorkerBootstrapTickerHandle.Reset();
		return false;
	}

	UOutlierArenaPoolSubsystem* ArenaPool =
		World->GetSubsystem<UOutlierArenaPoolSubsystem>();
	const AOutlierGameMode* ArenaGameMode = World->GetAuthGameMode<AOutlierGameMode>();
	const bool bContentReady = ArenaGameMode
		&& ArenaGameMode->IsArenaWorkerPreloadReady()
		&& ArenaPool
		&& ArenaPool->IsArenaContentReady(0);
	if (!bContentReady)
	{
		ArenaWorkerReadyStableFrames = 0;
		return true;
	}

	++ArenaWorkerReadyStableFrames;
	if (ArenaWorkerReadyStableFrames < RequiredReadyStableFrames)
	{
		return true;
	}

	ArenaWorkerBootstrapTickerHandle.Reset();
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaWorker] WP cells and LevelInstances stable for %d frames; notifying Worker Ready"),
		RequiredReadyStableFrames);
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaWorker] Ready on persistent arena map %s"),
		Settings ? *Settings->GetArenaPackageName() : TEXT("None"));
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem =
		GetSubsystem<UOutlierArenaProcessSubsystem>())
	{
		ProcessSubsystem->NotifyArenaWorldReady(World);
	}
	return false;
}

void UOutlierGameInstance::HandlePreLoadMap(const FString& MapName)
{
	if (IsRunningDedicatedServer()) return;
	// Loading Widget 표시
}

bool UOutlierGameInstance::TryQueueLobbyRecovery()
{
	if (!bArenaHandoffActive || bLobbyRecoveryAttempted)
	{
		return false;
	}

	bLobbyRecoveryQueued = true;
	bLobbyRecoveryAttempted = true;
	return true;
}

bool UOutlierGameInstance::TravelToLobby(UWorld* World)
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const FString LobbyAddress = Settings
		? Settings->LobbyAddress.TrimStartAndEnd()
		: FString();
	APlayerController* PlayerController = World
		? World->GetFirstPlayerController()
		: nullptr;
	if (!Settings
		|| !Settings->bReturnToLobbyOnMatchEnd
		|| LobbyAddress.IsEmpty()
		|| !PlayerController)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaReturn] Lobby recovery is not configured"));
		return false;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ArenaReturn] Traveling to Lobby %s"),
		*LobbyAddress);
	PlayerController->ClientTravel(LobbyAddress, TRAVEL_Absolute);
	return true;
}

void UOutlierGameInstance::ResetArenaHandoffState()
{
	bArenaHandoffActive = false;
	bLobbyRecoveryQueued = false;
	bLobbyRecoveryAttempted = false;
}
