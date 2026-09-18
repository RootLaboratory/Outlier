// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierArenaInstance.h"
#include "OutlierArenaSubsystem.generated.h"

class ULevelStreamingDynamic;
class ULevel;
class AActor;
class ACharacter;
class APlayerController;
class UDataLayerAsset;
class UDataLayerInstance;
class UDataLayerManager;
class UWorldPartitionSubsystem;
enum class EDataLayerRuntimeState : uint8;

// Reload는 각 단계의 실제 완료 이벤트로만 진행한다. Stalled는 현재 Phase를 유지한 채
// 재검사를 기다리는 상태이고, Failed만 해당 Generation을 더 진행할 수 없는 종료 상태다.
UENUM(BlueprintType)
enum class EOutlierGameplayReloadPhase : uint8
{
	Ready,
	WaitingForActorEndPlay,
	WaitingForGCPurge,
	WaitingForClientAcks,
	ActivatingGameplayData,
	WaitingForStreaming,
	Failed
};

UENUM(BlueprintType)
enum class EOutlierGameplayReloadFailure : uint8
{
	None,
	InvalidRuntime,
	DataLayerUnavailable,
	DataLayerStateChangeRejected,
	RequiredClientDisconnected,
	WorkerStallTimeout
};

DECLARE_MULTICAST_DELEGATE(FOnArenaShown);
DECLARE_MULTICAST_DELEGATE(FOnArenaReleased);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaGameplayGenerationEvent, uint32 /*GameplayGeneration*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnArenaGameplayReloadStalled,
	uint32 /*GameplayGeneration*/,
	EOutlierGameplayReloadPhase /*Phase*/,
	FString /*Diagnostic*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnArenaGameplayReloadFailed,
	uint32 /*GameplayGeneration*/,
	EOutlierGameplayReloadFailure /*Failure*/);

UCLASS()
class OUTLIER_API UOutlierArenaSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FOutlierArenaInstance* AcquireArena();
	void ReleaseArena();
	void ReloadArena();
	uint32 ReserveGameplayGeneration();
	bool ReloadGameplayData(uint32 InGameplayGeneration, bool bDeferActivation = false);
	void WaitForGameplayDataReady(uint32 InGameplayGeneration);
	void ActivateGameplayData(uint32 InGameplayGeneration);
	bool RetryStalledGameplayReload(uint32 InGameplayGeneration);
	void FailGameplayReload(uint32 InGameplayGeneration, EOutlierGameplayReloadFailure Failure);
	void DumpGameplayReloadState() const;
	bool IsGameplayDataLayerAvailable() const;
	void SuspendArenaVisibilityForConnection(APlayerController* PlayerController);
	ULevel* GetArenaLoadedLevel() const;
	bool IsPersistentArenaWorld() const;
	bool IsActorOwnedByArena(const AActor* Actor) const;
	const UWorld* GetArenaWorld() const;
	void EnsureArenaLoaded(bool bForceReload = false);
	bool IsArenaReady() const;
	bool IsArenaContentReady() const;
	bool IsStreamingArenaReady(const ULevelStreamingDynamic* StreamingLevel);
	uint32 GetGameplayGeneration() const { return GameplayGeneration; }
	EOutlierGameplayReloadPhase GetGameplayReloadPhase() const;
	bool IsGameplayReloadStalled(uint32 InGameplayGeneration) const;
	bool IsGameplayReloadGCVerified(uint32 InGameplayGeneration) const;
	static bool IsGameplayGenerationNewer(uint32 Candidate, uint32 Reference);
	static bool HasGameplayReloadTimedOut(double ElapsedSeconds, double TimeoutSeconds);

	FOnArenaShown OnArenaShown;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReloadStarted;
	FOnArenaGameplayGenerationEvent OnArenaGameplayGCReady;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReady;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReloadResumed;
	FOnArenaGameplayReloadStalled OnArenaGameplayReloadStalled;
	FOnArenaGameplayReloadFailed OnArenaGameplayReloadFailed;
	FOnArenaReleased OnArenaReleased;

	void HoldCharacterUntilArenaCellReady(ACharacter* Character);

private:
	struct FPendingGameplayReload;

	void PreloadArena();
	static const UWorld* GetOwningArenaWorld(const ULevel* Level);
	static const UWorld* ResolveArenaWorld(const FOutlierArenaInstance& InArena);
	ULevelStreamingDynamic* LoadArenaLevelInstance(const FTransform& InstanceTransform);
	void RefreshArenaReadyState(const TCHAR* Reason);

	void BeginDeferredReload(ULevelStreamingDynamic* StreamingLevel);
	void TickPendingReloads();
	const UWorld* ResolveDataLayerWorld() const;
	const UDataLayerInstance* ResolveGameplayDataLayer() const;
	bool SetGameplayDataLayerState(EDataLayerRuntimeState State) const;
	bool IsGameplayDataLayerState(EDataLayerRuntimeState State) const;
	bool AddPendingGameplayReload(uint32 Generation, bool bCanChangeState);
	void BindGameplayReloadEvents();
	void TryRequestGameplayReloadGC();
	void TryCompleteGameplayReloadActivation();
	void TickPendingGameplayReloadTimeouts();
	void SetGameplayReloadPhase(EOutlierGameplayReloadPhase NewPhase);
	void ReportStalledGameplayReload();
	FString BuildGameplayReloadDiagnostic() const;
	void ClearGameplayReloadActorBindings();
	static FString DescribeActorLevelPackage(const AActor* Actor);
	void ActivatePendingGameplayReload(uint32 Generation);
	void EnsureArenaGameplayDataActivated();
	void TickPendingInitialActivation();
	void HandleGameplayGarbageCollectComplete();
	void HandleGameplayStreamingStateUpdated();

	UFUNCTION()
	void HandleGameplayDataLayerStateChanged(
		const UDataLayerInstance* DataLayer,
		EDataLayerRuntimeState State);

	UFUNCTION()
	void HandleGameplayReloadActorEndPlay(
		AActor* Actor,
		EEndPlayReason::Type EndPlayReason);

	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> PendingReloadLevels;
	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> PendingGCLevels;
	bool bReloadGCRequested = false;
	FTimerHandle ReloadPollTimer;

	struct FPendingGameplayReload
	{
		uint32 Generation = 0;
		TWeakObjectPtr<UDataLayerInstance> DataLayerInstance;
		TWeakObjectPtr<UWorldPartitionSubsystem> WorldPartitionSubsystem;
		TArray<TWeakObjectPtr<AActor>> TrackedActors;
		TArray<TWeakObjectPtr<AActor>> ActorsAwaitingEndPlay;
		bool bLoadRequested = false;
		bool bGCRequested = false;
		bool bGCVerified = false;
		bool bCanChangeState = false;
		EOutlierGameplayReloadPhase Phase = EOutlierGameplayReloadPhase::Ready;
		EOutlierGameplayReloadFailure Failure = EOutlierGameplayReloadFailure::None;
		double PhaseStartTime = 0.0;
		bool bStallReported = false;
		bool bIsStalled = false;
	};
	TOptional<FPendingGameplayReload> PendingGameplayReload;
	FTimerHandle GameplayReloadTimeoutTimer;

	bool bInitialActivationPending = false;
	double InitialActivationStartTime = 0.0;
	FTimerHandle InitialActivationPollTimer;
	TArray<TWeakObjectPtr<UDataLayerManager>> BoundGameplayDataLayerManagers;
	TMap<TWeakObjectPtr<UWorldPartitionSubsystem>, FDelegateHandle> GameplayStreamingStateHandles;
	FDelegateHandle GameplayGarbageCollectCompleteHandle;

	UPROPERTY()
	TSoftObjectPtr<UDataLayerAsset> GameplayDataLayer;

	struct FPendingSpawnHold
	{
		TWeakObjectPtr<ACharacter> Character;
		double StartTime = 0.0;
	};
	TArray<FPendingSpawnHold> PendingSpawnHolds;
	FTimerHandle SpawnHoldPollTimer;
	void TickPendingSpawnHolds();
	bool AreArenaLevelInstancesLoaded() const;

	UFUNCTION()
	void HandleArenaLevelLoaded();

	UFUNCTION()
	void HandleArenaLevelShown();

	UPROPERTY()
	FOutlierArenaInstance Arena;

	UPROPERTY(EditDefaultsOnly, Category = "Arena")
	TSoftObjectPtr<UWorld> ArenaLevel;

	uint32 GameplayGeneration = 0;
};
