#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "Network/OutlierArenaProcessTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierArenaProcessSubsystem.generated.h"

class FSocket;
class UWorld;

DECLARE_MULTICAST_DELEGATE(FOutlierArenaSlotReadyDelegate);

UCLASS()
class OUTLIER_API UOutlierArenaProcessSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsLobbyManagerActive() const { return bLobbyManagerActive; }
	bool TryAllocateReadySlot(
		const FGuid& MatchId,
		FString& OutAddress,
		int32& OutSlotId);
	void ReleaseAllocation(const FGuid& MatchId);

	void NotifyArenaWorldReady(UWorld* ArenaWorld);
	void NotifyWorkerInMatch(const FGuid& MatchId);
	void NotifyWorkerReleasing(const FGuid& MatchId);
	bool CanWorkerAcceptMatch(const FGuid& MatchId) const;

	FOutlierArenaSlotReadyDelegate OnSlotReady;

private:
	struct FWorkerRuntime
	{
		FProcHandle ProcessHandle;
		FSocket* ControlSocket = nullptr;
		double StateChangedAt = 0.0;
		double RestartAt = 0.0;
		int32 RestartAttempts = 0;
	};

	struct FControlConnection
	{
		FSocket* Socket = nullptr;
		int32 SlotId = INDEX_NONE;
		TArray<uint8> ReceiveBuffer;
	};

	bool Tick(float DeltaTime);
	bool StartLobbyManager();
	bool StartControlListener();
	bool LaunchWorker(int32 SlotId);
	void PollLobby(double CurrentTime);
	void AcceptControlConnections();
	void PollControlConnections();
	void PollWorkerProcesses(double CurrentTime);
	void HandleLobbyMessage(
		FControlConnection& Connection,
		const FOutlierArenaControlMessage& Message);
	void HandleWorkerStopped(int32 SlotId, double CurrentTime);
	void CloseLobbyConnection(int32 ConnectionIndex);

	void PollWorker(double CurrentTime);
	bool ConnectWorkerControl();
	bool VerifyWorkerListenPort(UWorld* ArenaWorld) const;
	void ShutdownWorker(const FString& Reason);
	void HandleWorkerMessage(const FOutlierArenaControlMessage& Message);
	bool SendWorkerMessage(
		EOutlierArenaControlMessageType Type,
		const FGuid& MatchId = FGuid());

	bool SendMessage(FSocket* Socket, const FOutlierArenaControlMessage& Message) const;
	bool ReceiveMessages(
		FSocket* Socket,
		TArray<uint8>& ReceiveBuffer,
		TFunctionRef<void(const FOutlierArenaControlMessage&)> Handler) const;
	void DestroySocket(FSocket*& Socket) const;
	FString BuildWorkerArguments(int32 SlotId, int32 WorkerPort) const;
	FString ResolveWorkerExecutable() const;

	FOutlierArenaSlotRegistry SlotRegistry;
	TArray<FWorkerRuntime> WorkerRuntimes;
	TArray<FControlConnection> LobbyConnections;
	FSocket* ListenerSocket = nullptr;
	FSocket* WorkerControlSocket = nullptr;
	TArray<uint8> WorkerReceiveBuffer;
	FGuid WorkerExpectedMatchId;
	FTSTicker::FDelegateHandle TickerHandle;
	double LastWorkerConnectAttempt = 0.0;
	double LastWorkerHeartbeatSentAt = 0.0;
	double LastLobbyContactAt = 0.0;
	int32 WorkerSlotId = INDEX_NONE;
	int32 WorkerListenPort = 0;
	int32 ControlPort = 0;
	bool bLobbyManagerActive = false;
	bool bWorkerMode = false;
	bool bArenaWorldReady = false;
	bool bShuttingDown = false;
};
