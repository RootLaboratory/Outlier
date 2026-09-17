// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierGameInstance.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Network/OutlierArenaProcessSubsystem.h"
#include "Network/OutlierArenaSubsystem.h"
#include "UI/LoadingWidget.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"

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
	if (ArenaWorkerBootstrapTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaWorkerBootstrapTickerHandle);
		ArenaWorkerBootstrapTickerHandle.Reset();
	}
	if (ArenaReconnectTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaReconnectTickerHandle);
		ArenaReconnectTickerHandle.Reset();
	}

	if (GEngine && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}

	Super::Shutdown();
}

void UOutlierGameInstance::NotifyArenaHandoffStarted(const FString& ArenaUrl)
{
	bArenaHandoffActive = true;
	bLobbyRecoveryQueued = false;
	bLobbyRecoveryAttempted = false;
	// 재접속도 처음 Handoff와 같은 MatchId/PlayerId/Role 옵션을 보내야 Worker가
	// 시작된 매치의 원래 자리에 다시 받아주므로 주소 전체를 보관한다.
	LastArenaHandoffUrl = ArenaUrl;
	bArenaReconnectActive = false;
	ArenaReconnectDeadlineSeconds = 0.0;
}

void UOutlierGameInstance::PrepareForExplicitLeave()
{
	// 사용자가 직접 나가는 경우에는 뒤이어 발생하는 연결 종료를 장애로 오인해
	// 이전 Worker URL로 재접속하면 안 된다. Travel 전에 Handoff 수명을 여기서 끝낸다.
	ResetArenaHandoffState();
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
		if (Settings && Settings->IsArenaWorld(LoadedWorld))
		{
			// 접속에 성공하면 현재 재시도만 끝낸다. Handoff 표식과 URL은 Worker에 머무는 동안
			// 유지해야 이후 다시 연결이 끊겨도 같은 매치로 재접속할 수 있다.
			bArenaReconnectActive = false;
			ArenaReconnectDeadlineSeconds = 0.0;
			if (ArenaReconnectTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(ArenaReconnectTickerHandle);
				ArenaReconnectTickerHandle.Reset();
			}
		}
		else if (!bArenaReconnectActive)
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
	(void)NetDriver;
	// 외부 Arena Handoff가 활성화된 클라이언트만 Dedicated Worker 재접속 대상이다.
	// 일반 Listen Client는 Host가 사라진 것이므로 기다리지 않고 Title로 돌아간다.
	if (bArenaHandoffActive && !LastArenaHandoffUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReconnect] Arena connection lost. Retrying within grace period Type=%s Error=%s"),
			ENetworkFailure::ToString(FailureType),
			*ErrorString);
		ScheduleArenaReconnect();
		return;
	}
	if (!bArenaHandoffActive && World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaReturn] Listen host connection ended. Returning client to Title Type=%s Error=%s"),
			ENetworkFailure::ToString(FailureType),
			*ErrorString);
		ReturnToMainMenu();
		return;
	}

	if (!TryQueueLobbyRecovery())
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ArenaReturn] Arena connection failed. Lobby recovery queued Type=%s Error=%s"),
		ENetworkFailure::ToString(FailureType),
		*ErrorString);
}

void UOutlierGameInstance::ScheduleArenaReconnect()
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	// 연속 NetworkFailure마다 마감 시간을 다시 늘리면 Worker보다 오래 재시도하게 된다.
	// 최초 실패에서만 Deadline을 잡아 서버와 클라이언트의 유예 시간을 같은 기준으로 유지한다.
	if (!bArenaReconnectActive)
	{
		bArenaReconnectActive = true;
		ArenaReconnectDeadlineSeconds = FPlatformTime::Seconds()
			+ (Settings ? FMath::Max(Settings->ArenaWorkerReconnectGraceSeconds, 1.0f) : 30.0f);
	}
	if (!ArenaReconnectTickerHandle.IsValid())
	{
		const float RetrySeconds = Settings
			? FMath::Max(Settings->ArenaWorkerReconnectRetrySeconds, 0.1f)
			: 2.0f;
		ArenaReconnectTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UOutlierGameInstance::HandleArenaReconnectTick),
			RetrySeconds);
	}
}

bool UOutlierGameInstance::HandleArenaReconnectTick(float DeltaTime)
{
	(void)DeltaTime;
	// ClientTravel이 현재 World를 교체할 수 있으므로 Ticker는 매 시도마다 한 번만 실행한다.
	// 실패하면 NetworkFailure가 다음 시도를 다시 예약하고, 성공하면 PostLoadMap이 상태를 정리한다.
	ArenaReconnectTickerHandle.Reset();
	if (!bArenaReconnectActive || LastArenaHandoffUrl.IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (FPlatformTime::Seconds() >= ArenaReconnectDeadlineSeconds)
	{
		bArenaReconnectActive = false;
		UE_LOG(LogTemp, Warning, TEXT("[ArenaReconnect] Reconnect grace expired; returning to Lobby"));
		if (TryQueueLobbyRecovery())
		{
			bLobbyRecoveryQueued = false;
			TravelToLobby(World);
			ResetArenaHandoffState();
		}
		return false;
	}

	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		ScheduleArenaReconnect();
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("[ArenaReconnect] Retrying %s"), *LastArenaHandoffUrl);
	PlayerController->ClientTravel(LastArenaHandoffUrl, TRAVEL_Absolute);
	return false;
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

	UOutlierArenaSubsystem* ArenaSubsystem =
		World->GetSubsystem<UOutlierArenaSubsystem>();
	const AOutlierGameMode* ArenaGameMode = World->GetAuthGameMode<AOutlierGameMode>();
	const bool bContentReady = ArenaGameMode
		&& ArenaGameMode->IsArenaWorkerPreloadReady()
		&& ArenaSubsystem
		&& ArenaSubsystem->IsArenaContentReady();
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
	if (ArenaReconnectTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ArenaReconnectTickerHandle);
		ArenaReconnectTickerHandle.Reset();
	}
	bArenaHandoffActive = false;
	bLobbyRecoveryQueued = false;
	bLobbyRecoveryAttempted = false;
	bArenaReconnectActive = false;
	ArenaReconnectDeadlineSeconds = 0.0;
	LastArenaHandoffUrl.Reset();
}
