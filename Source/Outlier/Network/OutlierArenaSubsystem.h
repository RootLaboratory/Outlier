#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierArenaInstance.h"
#include "OutlierArenaSubsystem.generated.h"

class ULevel;
class ULevelStreaming;
class ULevelStreamingDynamic;
class AActor;
class FDataValidationContext;

UENUM(BlueprintType)
enum class EOutlierGameplayReloadPhase : uint8
{
	Ready,
	WaitingForUnload,
	WaitingForGCPurge,
	WaitingForClientAcks,
	WaitingForStreaming,
	Failed
};

UENUM(BlueprintType)
enum class EOutlierGameplayReloadFailure : uint8
{
	None,
	InvalidRuntime,
	GameplayLevelsUnavailable,
	LevelStateChangeRejected,
	RequiredClientDisconnected,
	WorkerStallTimeout
};

DECLARE_MULTICAST_DELEGATE(FOnArenaShown);
DECLARE_MULTICAST_DELEGATE(FOnArenaReleased);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArenaGameplayGenerationEvent, uint32 /*Generation*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnArenaGameplayReloadStalled,
	uint32 /*Generation*/,
	EOutlierGameplayReloadPhase /*Phase*/,
	FString /*Diagnostic*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnArenaGameplayReloadFailed,
	uint32 /*Generation*/,
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
	void EnsureArenaLoaded();
	uint32 ReserveGameplayGeneration();
	bool ReloadGameplayLevels(uint32 Generation, bool bWaitForClientAcks);
	bool BeginClientGameplayReload(uint32 Generation);
	void AllowGameplayLevelLoad(uint32 Generation);
	bool RetryStalledGameplayReload(uint32 Generation);
	void FailGameplayReload(uint32 Generation, EOutlierGameplayReloadFailure Failure);
	// void DumpGameplayReloadState() const;
	// void DumpGameplayActorState() const;

	bool IsGameplayLevelsConfigured() const;
	bool IsGameplayLevelsReady() const;
	bool IsGameplayReloadUnloaded(uint32 Generation) const;
	void SuspendGameplayVisibilityForConnection(APlayerController* PlayerController) const;
	ULevel* GetArenaLoadedLevel() const;
	bool IsPersistentArenaWorld() const;
	bool IsActorOwnedByArena(const AActor* Actor) const;
#if WITH_EDITOR
	// 에디터에 배치된 전투 Actor가 리로드 대상 Gameplay 서브레벨에 있는지 검사한다.
	static bool ValidateGameplayActorPlacement(
		const AActor* Actor, FDataValidationContext& Context);
	static bool IsGameplaySublevelPackage(
		const UWorld* ArenaMap, const FString& LevelPackageName);
#endif
	const UWorld* GetArenaWorld() const;
	bool IsArenaReady() const;
	bool IsArenaContentReady() const;
	uint32 GetGameplayGeneration() const { return GameplayGeneration; }
	EOutlierGameplayReloadPhase GetGameplayReloadPhase() const;
	bool IsGameplayReloadStalled(uint32 Generation) const;
	static bool IsGameplayGenerationNewer(uint32 Candidate, uint32 Reference);
	static bool HasGameplayReloadTimedOut(double ElapsedSeconds, double TimeoutSeconds);
	static bool CanCompleteGameplayReload(
		const TArray<TWeakObjectPtr<AActor>>& OldActors,
		bool bUnloaded,
		bool bCanLoad,
		bool bLevelsShown);

	FOnArenaShown OnArenaShown;
	FOnArenaReleased OnArenaReleased;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReloadStarted;
	FOnArenaGameplayGenerationEvent OnArenaGameplayUnloaded;
	// 기존 전투 시스템의 GC 경계 이벤트. 설정된 서브레벨과 이전 Actor가 모두 정리된 뒤 발생한다.
	FOnArenaGameplayGenerationEvent OnArenaGameplayGCReady;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReady;
	FOnArenaGameplayGenerationEvent OnArenaGameplayReloadResumed;
	FOnArenaGameplayReloadStalled OnArenaGameplayReloadStalled;
	FOnArenaGameplayReloadFailed OnArenaGameplayReloadFailed;

private:
	struct FPendingGameplayReload
	{
		uint32 Generation = 0;
		EOutlierGameplayReloadPhase Phase = EOutlierGameplayReloadPhase::Ready;
		EOutlierGameplayReloadFailure Failure = EOutlierGameplayReloadFailure::None;
		TArray<TWeakObjectPtr<AActor>> OldActors;
		// TArray<TArray<TWeakObjectPtr<AActor>>> OldActorsByLevel;
		// TArray<int32> OldActorCounts;
		// TArray<uint8> UnloadedLevelLogged;
		// TArray<uint8> ShownLevelLogged;
		TWeakObjectPtr<ULevel> StableArenaLevel;
		bool bCanLoad = false;
		bool bUnloaded = false;
		bool bGCRequested = false;
		bool bIsStalled = false;
		double PhaseStartTime = 0.0;
		double ReloadStartTime = 0.0;
		double LoadRequestTime = 0.0;
	};

	void PreloadArena();
	bool ResolveGameplaySublevels(const UWorld* ArenaWorld);
	bool ResolvePersistentGameplayLevels();
	bool CreateListenGameplayLevels();
	ULevelStreamingDynamic* LoadLevelInstance(
		const TSoftObjectPtr<UWorld>& Map, const FString& InstanceName);
	void RequestGameplayLevelsLoaded(bool bLoaded);
	bool AreGameplayLevelsUnloaded() const;
	bool AreGameplayLevelsShown() const;
	bool TickArenaLevels(float DeltaTime);
	void SetGameplayReloadPhase(EOutlierGameplayReloadPhase Phase);
	FString BuildGameplayReloadDiagnostic() const;
	void CaptureGameplayReloadActors(FPendingGameplayReload& Pending);
	// void LogGameplayLevelState(uint32 Generation, const TCHAR* Event) const;
	void EnsureArenaPollTicker();
	void RefreshArenaReadyState();

	UPROPERTY(Transient)
	FOutlierArenaInstance Arena;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreaming>> GameplayStreamingLevels;

	TArray<TWeakObjectPtr<ULevelStreaming>> ReleasingLevels;
	TOptional<FPendingGameplayReload> PendingGameplayReload;
	FTSTicker::FDelegateHandle ArenaPollTickerHandle;
	TArray<TSoftObjectPtr<UWorld>> GameplaySublevels;
	TSoftObjectPtr<UWorld> ArenaLevel;
	uint32 GameplayGeneration = 0;
	bool bGameplayLevelsResolved = false;
	bool bInitialGameplayLevelsRequested = false;
	bool bPreloadAfterRelease = false;
	bool bArenaShownBroadcast = false;
	bool bInitialLoadStallLogged = false;
	double InitialLoadStartTime = 0.0;
};
