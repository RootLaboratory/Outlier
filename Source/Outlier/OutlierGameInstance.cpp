// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierGameInstance.h"
#include "OutlierArenaSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "UI/LoadingWidget.h"
#include "Misc/Parse.h"

void UOutlierGameInstance::Init()
{
	Super::Init();

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
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaWorker] Ready on persistent arena map %s"),
			*Settings->GetArenaPackageName());
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
