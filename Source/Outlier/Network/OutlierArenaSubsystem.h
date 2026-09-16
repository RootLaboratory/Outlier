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

DECLARE_MULTICAST_DELEGATE(FOnArenaShown);
DECLARE_MULTICAST_DELEGATE(FOnArenaReleased);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaGameplayGenerationEvent, uint32 /*GameplayGeneration*/);

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
	static bool IsGameplayGenerationNewer(uint32 Candidate, uint32 Reference);

	FOnArenaShown OnArenaShown;
	FOnArenaGameplayGenerationEvent OnArenaGameplayGCReady;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReady;
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
	void AbandonStalledGameplayReload(uint32 Generation);
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
		bool bCanChangeState = false;
		double StartTime = 0.0;
		bool bStallReported = false;
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
