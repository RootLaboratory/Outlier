#include "Network/OutlierArenaProcessSubsystem.h"

#include "Common/TcpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "OutlierArenaSettings.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
constexpr float ControlPollIntervalSeconds = 0.1f;
constexpr double WorkerConnectRetrySeconds = 0.5;
constexpr int32 SocketReadChunkSize = 4096;
}

bool UOutlierArenaProcessSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || !IsRunningDedicatedServer())
	{
		return false;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const bool bIsWorker = FParse::Param(FCommandLine::Get(), TEXT("OutlierArenaWorker"));
	return Settings
		&& ((bIsWorker && Settings->bUseArenaControlChannel)
			|| (!bIsWorker && Settings->bUseProcessManager));
}

void UOutlierArenaProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bWorkerMode = FParse::Param(FCommandLine::Get(), TEXT("OutlierArenaWorker"));

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!Settings)
	{
		return;
	}

	ControlPort = Settings->ArenaControlPort;
	if (bWorkerMode)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ArenaSlot="), WorkerSlotId);
		FParse::Value(FCommandLine::Get(), TEXT("ArenaControlPort="), ControlPort);
		if (WorkerSlotId < 0 || ControlPort <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[ArenaProcess] Invalid Worker control arguments"));
			return;
		}
	}
	else if (!StartLobbyManager())
	{
		return;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOutlierArenaProcessSubsystem::Tick),
		ControlPollIntervalSeconds);
}

void UOutlierArenaProcessSubsystem::Deinitialize()
{
	bShuttingDown = true;
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	DestroySocket(WorkerControlSocket);
	for (int32 ConnectionIndex = LobbyConnections.Num() - 1; ConnectionIndex >= 0; --ConnectionIndex)
	{
		CloseLobbyConnection(ConnectionIndex);
	}
	DestroySocket(ListenerSocket);

	if (bLobbyManagerActive)
	{
		for (FWorkerRuntime& Runtime : WorkerRuntimes)
		{
			if (Runtime.ProcessHandle.IsValid())
			{
				FPlatformProcess::TerminateProc(Runtime.ProcessHandle, true);
				FPlatformProcess::CloseProc(Runtime.ProcessHandle);
			}
		}
	}

	WorkerRuntimes.Reset();
	Super::Deinitialize();
}

bool UOutlierArenaProcessSubsystem::StartLobbyManager()
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!Settings || !Settings->bUseProcessManager || !Settings->bUseArenaControlChannel)
	{
		return false;
	}

	SlotRegistry.Initialize(
		Settings->ArenaWorkerHost,
		Settings->ArenaBasePort,
		Settings->StaticArenaSlots);
	WorkerRuntimes.SetNum(SlotRegistry.Num());
	if (!StartControlListener())
	{
		return false;
	}

	bLobbyManagerActive = true;
	for (int32 SlotId = 0; SlotId < SlotRegistry.Num(); ++SlotId)
	{
		LaunchWorker(SlotId);
	}
	return true;
}

bool UOutlierArenaProcessSubsystem::StartControlListener()
{
	ListenerSocket = FTcpSocketBuilder(TEXT("OutlierArenaControlListener"))
		.AsReusable()
		.BoundToAddress(FIPv4Address::InternalLoopback)
		.BoundToPort(ControlPort)
		.Listening(8)
		.AsNonBlocking();
	if (!ListenerSocket)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaProcess] Failed to listen on 127.0.0.1:%d"),
			ControlPort);
		return false;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ArenaProcess] Control listener ready on 127.0.0.1:%d"),
		ControlPort);
	return true;
}

bool UOutlierArenaProcessSubsystem::LaunchWorker(int32 SlotId)
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	FOutlierArenaSlotRecord* Slot = SlotRegistry.FindSlot(SlotId);
	if (!Settings || !Slot || !WorkerRuntimes.IsValidIndex(SlotId))
	{
		return false;
	}

	FWorkerRuntime& Runtime = WorkerRuntimes[SlotId];
	const FString Executable = ResolveWorkerExecutable();
	const int32 WorkerPort = Settings->ArenaBasePort + SlotId;
	const FString Arguments = BuildWorkerArguments(SlotId, WorkerPort);
	uint32 ProcessId = 0;
	Runtime.ProcessHandle = FPlatformProcess::CreateProc(
		*Executable,
		*Arguments,
		false,
		true,
		true,
		&ProcessId,
		0,
		*FPaths::GetPath(Executable),
		nullptr,
		nullptr);

	const double CurrentTime = FPlatformTime::Seconds();
	Runtime.StateChangedAt = CurrentTime;
	if (!Runtime.ProcessHandle.IsValid() || ProcessId == 0)
	{
		++Runtime.RestartAttempts;
		Runtime.RestartAt = CurrentTime + Settings->ArenaWorkerRestartDelaySeconds;
		SlotRegistry.MarkFailed(SlotId);
		UE_LOG(LogTemp, Error,
			TEXT("[ArenaProcess] Failed to launch Worker Slot=%d Executable=%s"),
			SlotId,
			*Executable);
		return false;
	}

	SlotRegistry.MarkStarting(SlotId, ProcessId);
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaProcess] Worker launched Slot=%d PID=%u Address=%s"),
		SlotId,
		ProcessId,
		*Slot->Address);
	return true;
}

bool UOutlierArenaProcessSubsystem::Tick(float DeltaTime)
{
	(void)DeltaTime;
	if (bShuttingDown)
	{
		return false;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	if (bWorkerMode)
	{
		PollWorker(CurrentTime);
	}
	else if (bLobbyManagerActive)
	{
		PollLobby(CurrentTime);
	}
	return true;
}

void UOutlierArenaProcessSubsystem::PollLobby(double CurrentTime)
{
	AcceptControlConnections();
	PollControlConnections();
	PollWorkerProcesses(CurrentTime);
}

void UOutlierArenaProcessSubsystem::AcceptControlConnections()
{
	if (!ListenerSocket)
	{
		return;
	}

	bool bHasPendingConnection = false;
	while (ListenerSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
	{
		FSocket* AcceptedSocket = ListenerSocket->Accept(
			TEXT("OutlierArenaWorkerControl"));
		if (!AcceptedSocket)
		{
			return;
		}

		AcceptedSocket->SetNonBlocking(true);
		AcceptedSocket->SetNoDelay(true);
		FControlConnection& Connection = LobbyConnections.AddDefaulted_GetRef();
		Connection.Socket = AcceptedSocket;
	}
}

void UOutlierArenaProcessSubsystem::PollControlConnections()
{
	for (int32 ConnectionIndex = LobbyConnections.Num() - 1;
		ConnectionIndex >= 0;
		--ConnectionIndex)
	{
		FControlConnection& Connection = LobbyConnections[ConnectionIndex];
		if (!Connection.Socket
			|| Connection.Socket->GetConnectionState() != SCS_Connected
			|| !ReceiveMessages(
				Connection.Socket,
				Connection.ReceiveBuffer,
				[this, &Connection](const FOutlierArenaControlMessage& Message)
				{
					HandleLobbyMessage(Connection, Message);
				}))
		{
			CloseLobbyConnection(ConnectionIndex);
		}
	}
}

void UOutlierArenaProcessSubsystem::PollWorkerProcesses(double CurrentTime)
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	if (!Settings)
	{
		return;
	}

	for (int32 SlotId = 0; SlotId < WorkerRuntimes.Num(); ++SlotId)
	{
		FWorkerRuntime& Runtime = WorkerRuntimes[SlotId];
		const FOutlierArenaSlotRecord* Slot = SlotRegistry.FindSlot(SlotId);
		if (!Slot)
		{
			continue;
		}

		if (Runtime.ProcessHandle.IsValid()
			&& !FPlatformProcess::IsProcRunning(Runtime.ProcessHandle))
		{
			HandleWorkerStopped(SlotId, CurrentTime);
			continue;
		}

		if (Runtime.ProcessHandle.IsValid()
			&& !Runtime.ControlSocket
			&& (Slot->State == EOutlierArenaSlotState::Ready
				|| Slot->State == EOutlierArenaSlotState::Allocated
				|| Slot->State == EOutlierArenaSlotState::InMatch))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ArenaProcess] Worker control connection closed Slot=%d"),
				SlotId);
			FPlatformProcess::TerminateProc(Runtime.ProcessHandle, true);
			HandleWorkerStopped(SlotId, CurrentTime);
			continue;
		}

		if (Slot->State == EOutlierArenaSlotState::Starting
			&& CurrentTime - Runtime.StateChangedAt
				> Settings->ArenaWorkerReadyTimeoutSeconds)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ArenaProcess] Worker Ready timeout Slot=%d"),
				SlotId);
			FPlatformProcess::TerminateProc(Runtime.ProcessHandle, true);
			HandleWorkerStopped(SlotId, CurrentTime);
			continue;
		}

		if (Slot->State == EOutlierArenaSlotState::Failed
			&& Runtime.RestartAttempts <= Settings->MaxArenaWorkerRestartAttempts
			&& CurrentTime >= Runtime.RestartAt)
		{
			LaunchWorker(SlotId);
		}
	}
}

void UOutlierArenaProcessSubsystem::HandleLobbyMessage(
	FControlConnection& Connection,
	const FOutlierArenaControlMessage& Message)
{
	FOutlierArenaSlotRecord* Slot = SlotRegistry.FindSlot(Message.SlotId);
	if (!Slot || !WorkerRuntimes.IsValidIndex(Message.SlotId)
		|| Message.ProcessId != Slot->ProcessId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaProcess] Rejected unknown Worker control message"));
		return;
	}

	FWorkerRuntime& Runtime = WorkerRuntimes[Message.SlotId];
	switch (Message.Type)
	{
	case EOutlierArenaControlMessageType::Ready:
		if (!SlotRegistry.MarkReady(Message.SlotId, Message.ProcessId))
		{
			return;
		}
		Connection.SlotId = Message.SlotId;
		Runtime.ControlSocket = Connection.Socket;
		Runtime.RestartAttempts = 0;
		Runtime.StateChangedAt = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaProcess] Worker Ready Slot=%d PID=%u"),
			Message.SlotId,
			Message.ProcessId);
		OnSlotReady.Broadcast();
		break;

	case EOutlierArenaControlMessageType::InMatch:
		if (SlotRegistry.MarkInMatch(Message.SlotId, Message.MatchId))
		{
			Runtime.StateChangedAt = FPlatformTime::Seconds();
		}
		break;

	case EOutlierArenaControlMessageType::Releasing:
		if (SlotRegistry.MarkReleasing(Message.SlotId, Message.MatchId))
		{
			Runtime.StateChangedAt = FPlatformTime::Seconds();
		}
		break;

	default:
		break;
	}
}

void UOutlierArenaProcessSubsystem::HandleWorkerStopped(
	int32 SlotId,
	double CurrentTime)
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	FOutlierArenaSlotRecord* Slot = SlotRegistry.FindSlot(SlotId);
	if (!Settings || !Slot || !WorkerRuntimes.IsValidIndex(SlotId))
	{
		return;
	}

	FWorkerRuntime& Runtime = WorkerRuntimes[SlotId];
	const bool bExpectedExit = Slot->State == EOutlierArenaSlotState::Releasing;
	if (Runtime.ProcessHandle.IsValid())
	{
		FPlatformProcess::CloseProc(Runtime.ProcessHandle);
	}

	for (int32 ConnectionIndex = LobbyConnections.Num() - 1;
		ConnectionIndex >= 0;
		--ConnectionIndex)
	{
		if (LobbyConnections[ConnectionIndex].Socket == Runtime.ControlSocket)
		{
			CloseLobbyConnection(ConnectionIndex);
			break;
		}
	}
	Runtime.ControlSocket = nullptr;
	Runtime.RestartAttempts = bExpectedExit ? 0 : Runtime.RestartAttempts + 1;
	Runtime.RestartAt = CurrentTime + Settings->ArenaWorkerRestartDelaySeconds;
	Runtime.StateChangedAt = CurrentTime;
	SlotRegistry.MarkFailed(SlotId);

	if (bExpectedExit)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ArenaProcess] Worker stopped Slot=%d Expected=true RestartAttempt=%d"),
			SlotId,
			Runtime.RestartAttempts);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArenaProcess] Worker stopped Slot=%d Expected=false RestartAttempt=%d"),
			SlotId,
			Runtime.RestartAttempts);
	}
}

void UOutlierArenaProcessSubsystem::CloseLobbyConnection(int32 ConnectionIndex)
{
	if (!LobbyConnections.IsValidIndex(ConnectionIndex))
	{
		return;
	}

	FControlConnection& Connection = LobbyConnections[ConnectionIndex];
	if (WorkerRuntimes.IsValidIndex(Connection.SlotId)
		&& WorkerRuntimes[Connection.SlotId].ControlSocket == Connection.Socket)
	{
		WorkerRuntimes[Connection.SlotId].ControlSocket = nullptr;
	}
	DestroySocket(Connection.Socket);
	LobbyConnections.RemoveAtSwap(ConnectionIndex, 1, EAllowShrinking::No);
}

bool UOutlierArenaProcessSubsystem::TryAllocateReadySlot(
	const FGuid& MatchId,
	FString& OutAddress,
	int32& OutSlotId)
{
	OutAddress.Reset();
	OutSlotId = INDEX_NONE;
	if (!bLobbyManagerActive)
	{
		return false;
	}

	FOutlierArenaSlotRecord Slot;
	if (!SlotRegistry.TryAllocate(MatchId, Slot)
		|| !WorkerRuntimes.IsValidIndex(Slot.SlotId))
	{
		return false;
	}
	if (!WorkerRuntimes[Slot.SlotId].ControlSocket)
	{
		SlotRegistry.ReleaseAllocation(MatchId);
		return false;
	}

	FOutlierArenaControlMessage Message;
	Message.Type = EOutlierArenaControlMessageType::Allocate;
	Message.SlotId = Slot.SlotId;
	Message.MatchId = MatchId;
	if (!SendMessage(WorkerRuntimes[Slot.SlotId].ControlSocket, Message))
	{
		SlotRegistry.ReleaseAllocation(MatchId);
		return false;
	}

	OutAddress = Slot.Address;
	OutSlotId = Slot.SlotId;
	UE_LOG(LogTemp, Display,
		TEXT("[ArenaProcess] Slot allocated Slot=%d Match=%s Address=%s"),
		OutSlotId,
		*MatchId.ToString(),
		*OutAddress);
	return true;
}

void UOutlierArenaProcessSubsystem::ReleaseAllocation(const FGuid& MatchId)
{
	for (const FOutlierArenaSlotRecord& Slot : SlotRegistry.GetSlots())
	{
		if (Slot.MatchId != MatchId || !WorkerRuntimes.IsValidIndex(Slot.SlotId))
		{
			continue;
		}

		FOutlierArenaControlMessage Message;
		Message.Type = EOutlierArenaControlMessageType::Release;
		Message.SlotId = Slot.SlotId;
		Message.MatchId = MatchId;
		SendMessage(WorkerRuntimes[Slot.SlotId].ControlSocket, Message);
		break;
	}
	SlotRegistry.ReleaseAllocation(MatchId);
	OnSlotReady.Broadcast();
}

void UOutlierArenaProcessSubsystem::NotifyArenaWorldReady()
{
	if (!bWorkerMode || bArenaWorldReady)
	{
		return;
	}

	bArenaWorldReady = true;
	ConnectWorkerControl();
}

void UOutlierArenaProcessSubsystem::NotifyWorkerInMatch(const FGuid& MatchId)
{
	if (bWorkerMode && WorkerExpectedMatchId == MatchId)
	{
		SendWorkerMessage(EOutlierArenaControlMessageType::InMatch, MatchId);
	}
}

void UOutlierArenaProcessSubsystem::NotifyWorkerReleasing(const FGuid& MatchId)
{
	if (bWorkerMode && WorkerExpectedMatchId == MatchId)
	{
		SendWorkerMessage(EOutlierArenaControlMessageType::Releasing, MatchId);
	}
}

bool UOutlierArenaProcessSubsystem::CanWorkerAcceptMatch(const FGuid& MatchId) const
{
	// 제어 채널 없이 직접 실행한 Worker는 기존 수동 QA 경로를 유지한다.
	return !bWorkerMode
		|| !WorkerControlSocket
		|| (WorkerExpectedMatchId.IsValid() && WorkerExpectedMatchId == MatchId);
}

void UOutlierArenaProcessSubsystem::PollWorker(double CurrentTime)
{
	if (!bArenaWorldReady)
	{
		return;
	}

	if (!WorkerControlSocket)
	{
		if (CurrentTime - LastWorkerConnectAttempt >= WorkerConnectRetrySeconds)
		{
			ConnectWorkerControl();
		}
		return;
	}

	if (WorkerControlSocket->GetConnectionState() != SCS_Connected
		|| !ReceiveMessages(
			WorkerControlSocket,
			WorkerReceiveBuffer,
			[this](const FOutlierArenaControlMessage& Message)
			{
				HandleWorkerMessage(Message);
			}))
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaProcess] Worker lost Lobby control connection"));
		DestroySocket(WorkerControlSocket);
		RequestEngineExit(TEXT("Arena Worker control connection lost"));
	}
}

bool UOutlierArenaProcessSubsystem::ConnectWorkerControl()
{
	LastWorkerConnectAttempt = FPlatformTime::Seconds();
	DestroySocket(WorkerControlSocket);

	bool bValidAddress = false;
	TSharedRef<FInternetAddr> Address = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
		->CreateInternetAddr();
	Address->SetIp(TEXT("127.0.0.1"), bValidAddress);
	Address->SetPort(ControlPort);
	if (!bValidAddress)
	{
		return false;
	}

	WorkerControlSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(
		NAME_Stream,
		TEXT("OutlierArenaWorkerControl"),
		false);
	if (!WorkerControlSocket)
	{
		return false;
	}

	WorkerControlSocket->SetNoDelay(true);
	WorkerControlSocket->SetNonBlocking(false);
	if (!WorkerControlSocket->Connect(*Address))
	{
		DestroySocket(WorkerControlSocket);
		return false;
	}
	WorkerControlSocket->SetNonBlocking(true);

	if (!SendWorkerMessage(EOutlierArenaControlMessageType::Ready))
	{
		DestroySocket(WorkerControlSocket);
		return false;
	}
	return true;
}

void UOutlierArenaProcessSubsystem::HandleWorkerMessage(
	const FOutlierArenaControlMessage& Message)
{
	if (Message.SlotId != WorkerSlotId)
	{
		return;
	}

	if (Message.Type == EOutlierArenaControlMessageType::Allocate)
	{
		if (!WorkerExpectedMatchId.IsValid())
		{
			WorkerExpectedMatchId = Message.MatchId;
		}
	}
	else if (Message.Type == EOutlierArenaControlMessageType::Release
		&& WorkerExpectedMatchId == Message.MatchId)
	{
		WorkerExpectedMatchId.Invalidate();
	}
}

bool UOutlierArenaProcessSubsystem::SendWorkerMessage(
	EOutlierArenaControlMessageType Type,
	const FGuid& MatchId)
{
	FOutlierArenaControlMessage Message;
	Message.Type = Type;
	Message.SlotId = WorkerSlotId;
	Message.ProcessId = FPlatformProcess::GetCurrentProcessId();
	Message.MatchId = MatchId;
	return SendMessage(WorkerControlSocket, Message);
}

bool UOutlierArenaProcessSubsystem::SendMessage(
	FSocket* Socket,
	const FOutlierArenaControlMessage& Message) const
{
	if (!Socket)
	{
		return false;
	}

	TArray<uint8> Frame;
	if (!OutlierArenaControl::EncodeFrame(Message, Frame))
	{
		return false;
	}

	int32 TotalSent = 0;
	while (TotalSent < Frame.Num())
	{
		int32 Sent = 0;
		if (!Socket->Send(Frame.GetData() + TotalSent, Frame.Num() - TotalSent, Sent)
			|| Sent <= 0)
		{
			return false;
		}
		TotalSent += Sent;
	}
	return true;
}

bool UOutlierArenaProcessSubsystem::ReceiveMessages(
	FSocket* Socket,
	TArray<uint8>& ReceiveBuffer,
	TFunctionRef<void(const FOutlierArenaControlMessage&)> Handler) const
{
	uint32 PendingBytes = 0;
	while (Socket->HasPendingData(PendingBytes) && PendingBytes > 0)
	{
		const int32 BytesToRead = FMath::Min<int32>(PendingBytes, SocketReadChunkSize);
		const int32 Offset = ReceiveBuffer.AddUninitialized(BytesToRead);
		int32 BytesRead = 0;
		if (!Socket->Recv(ReceiveBuffer.GetData() + Offset, BytesToRead, BytesRead)
			|| BytesRead <= 0)
		{
			ReceiveBuffer.SetNum(Offset, EAllowShrinking::No);
			return false;
		}
		ReceiveBuffer.SetNum(Offset + BytesRead, EAllowShrinking::No);
	}

	while (!ReceiveBuffer.IsEmpty())
	{
		FOutlierArenaControlMessage Message;
		FString Error;
		const EOutlierArenaFrameDecodeResult Result = OutlierArenaControl::TryDecodeFrame(
			ReceiveBuffer,
			Message,
			Error);
		if (Result == EOutlierArenaFrameDecodeResult::NeedMoreData)
		{
			break;
		}
		if (Result == EOutlierArenaFrameDecodeResult::Invalid)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ArenaProcess] %s"), *Error);
			return false;
		}
		Handler(Message);
	}
	return true;
}

void UOutlierArenaProcessSubsystem::DestroySocket(FSocket*& Socket) const
{
	if (!Socket)
	{
		return;
	}

	Socket->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
	Socket = nullptr;
}

FString UOutlierArenaProcessSubsystem::ResolveWorkerExecutable() const
{
	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const FString ConfiguredPath = Settings
		? Settings->ArenaWorkerExecutablePath.TrimStartAndEnd()
		: FString();
	return ConfiguredPath.IsEmpty()
		? FPlatformProcess::ExecutablePath()
		: FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

FString UOutlierArenaProcessSubsystem::BuildWorkerArguments(
	int32 SlotId,
	int32 WorkerPort) const
{
	FString Arguments;
#if WITH_EDITOR
	FString ProjectFile = FPaths::GetProjectFilePath();
	if (ProjectFile.IsEmpty())
	{
		ProjectFile = FPaths::ConvertRelativePathToFull(
			FString::Printf(TEXT("%s.uproject"), FApp::GetProjectName()));
	}
	Arguments += FString::Printf(TEXT("\"%s\" "), *ProjectFile);
#endif

	const FString LogDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectLogDir(), TEXT("ArenaWorkers")));
	IFileManager::Get().MakeDirectory(*LogDirectory, true);
	const FString WorkerLogPath = FPaths::Combine(
		LogDirectory,
		FString::Printf(TEXT("ArenaWorker%d.log"), SlotId));
	Arguments += FString::Printf(
		TEXT("-server -unattended -log -stdout -FullStdOutLogOutput ")
		TEXT("-abslog=\"%s\" -port=%d -OutlierArenaWorker ")
		TEXT("-ArenaSlot=%d -ArenaControlPort=%d"),
		*WorkerLogPath,
		WorkerPort,
		SlotId,
		ControlPort);

	float AutoCompleteSeconds = 0.0f;
	if (FParse::Value(
		FCommandLine::Get(),
		TEXT("OutlierArenaAutoCompleteSeconds="),
		AutoCompleteSeconds)
		&& AutoCompleteSeconds > 0.0f)
	{
		Arguments += FString::Printf(
			TEXT(" -OutlierArenaAutoCompleteSeconds=%.3f"),
			AutoCompleteSeconds);
	}
	return Arguments;
}
