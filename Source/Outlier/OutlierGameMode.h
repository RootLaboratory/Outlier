// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameMode.h"
#include "Network/OutlierMatchRequest.h"
#include "OutlierGameMode.generated.h"

class APlayerController;
class AShooterPlayerController;
class APartnerPlayerController;
class AShooterCharacter;
class APartnerCharacter;
class AOutlierCheckpoint;
class AOutlierPlayerState;
class AOutlierArenaPausePlayerState;
class UWorldPartitionStreamingSourceComponent;
class UDataTable;
enum class EOutlierPlayerRole : uint8;
struct FOutlierCheckpointData;

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AOutlierGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	AOutlierGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	bool IsArenaWorkerPreloadReady() const;

	void RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint);
	void RefreshPairLinks(AOutlierPlayerState* TriggeringPlayerState);

	UFUNCTION()
	void HandlePlayerDeath(AShooterCharacter* Character);

	// PreSetLoadWidget에서 페어 한쪽이 스테이지를 고를 때마다 호출됨.
	// 페어 양쪽이 같은 StageId를 고른 순간에만 실제로 RequestPresetRespawn까지 진행한다.
	void HandlePresetStageSelected(AController* Requester, FName StageId);

	// StageId에 대응하는 APresetPlayerStart 위치로 페어를 리스폰시킨다 (레벨 리셋 + 업그레이드 노드 플러시 포함).
	void RequestPresetRespawn(AController* Requester, FName StageId);

	void StartMatchedPair(
		AController* FirstController,
		AController* SecondController,
		int32 PairId,
		int32 ArenaId,
		EOutlierPlayerRole FirstRole,
		EOutlierPlayerRole SecondRole);

	void OnClientArenaReady(APlayerController* PC);
	void OnClientArenaGameplayGCReady(APlayerController* PC, int32 ArenaId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Network|Arena")
	bool CompleteArenaMatch();

	// 디버그: 요청한 페어의 arena를 통째로 리로드하고 시작점에 재스폰
	void DebugReloadArena(AController* Requester);


private:
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<APawn>> PendingPossessions;

	// Data Layer gameplay reload 완료 후 possess할 로컬 PC들
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<APawn>> PendingLocalPossessions;

	void HandleServerArenaReloaded(int32 ReloadedArenaId);

	int32 ReloadingArenaId = INDEX_NONE;
	FDelegateHandle ArenaShownHandle;
	int32 PendingGameplayGCArenaId = INDEX_NONE;
	TSet<TWeakObjectPtr<APlayerController>> PendingGameplayGCPlayers;
	TSet<TWeakObjectPtr<APlayerController>> ReadyGameplayGCPlayers;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	TSubclassOf<AShooterCharacter> ShooterClass;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	TSubclassOf<APartnerCharacter> PartnerClass;

	UPROPERTY(EditDefaultsOnly, Category = "Controller")
	TSubclassOf<AShooterPlayerController> ShooterControllerClass;

	UPROPERTY(EditDefaultsOnly, Category = "Controller")
	TSubclassOf<APartnerPlayerController> PartnerControllerClass;

	// 프리셋별 노드 제공량 테이블(DT_PresetNode)은 UOutlierArenaSettings로 옮겼다.
	// GameMode BP가 3개(BP_OutlierGM / BP_LobbyGM / BP_BTestGM)라 어느 게 뜨느냐에 따라
	// 값이 있기도 없기도 했다 — Listen은 Title 맵의 BP_LobbyGM이 GameMode다.

protected:
	virtual void PreLogin(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;

	// ArenaWorker는 접속 URL(?Role=Shooter)로 역할이 이미 확정된 상태로 들어온다.
	// 공용 PC로 받았다가 나중에 SwapPlayerControllers로 교체하면, 교체 창 동안 클라 월드에
	// 소유 커넥션 없는 PC가 남아 WP 셀 가시화 RPC가 폐기된다(재전송 없음 → 스트리밍 영구 정지).
	// 처음부터 역할별 PC로 스폰해서 그 창 자체를 없앤다.
	virtual APlayerController* SpawnPlayerController(
		ENetRole InRemoteRole,
		const FString& Options) override;
	virtual FString InitNewPlayer(
		APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void InitGameState() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void RespawnPairAtCheckpoint(AController* Controller);
	bool ResolveCheckpointTransform(AController* Controller, int32 ArenaId, FTransform& OutTransform) const;
	FString GetPlayerSaveId(AController* Controller) const;

	// 사망한 컨트롤러의 페어 양쪽에 PreSetLoadWidget을 띄운다. 실제 사람 컨트롤러가 하나도 없으면
	// (봇/아레나워커) 기존 RespawnPairAtCheckpoint 즉시 리스폰으로 폴백한다.
	void BeginPresetRespawnSelection(AController* Controller);

	// StageId를 가진 APresetPlayerStart를 찾아 스폰 위치를 돌려준다. RequiredArenaId를 지정하면
	// (StartMatchedPair처럼 이미 배정된 아레나 인스턴스가 있을 때) 그 아레나 소속인 것만 인정한다.
	// INDEX_NONE이면(사망 후 스테이지 점프처럼 아레나를 안 가리는 경우) 처음 찾은 것을 그대로 쓴다.
	bool ResolvePresetStageSpawn(
		FName StageId,
		int32& OutArenaId,
		FTransform& OutShooterSpawn,
		FTransform& OutPartnerSpawn,
		int32 RequiredArenaId = INDEX_NONE) const;
	int32 ResolvePresetNodeCount(FName StageId) const;
	void FlushUpgradeNodesForPair(AOutlierPlayerState* TriggeringPlayerState, int32 NewNodeCount);

	// DebugReloadArena/RequestPresetRespawn이 공유하는 "아레나 리로드 대기 후 페어 스폰/possess" 공통 로직.
	void ReloadArenaAndRespawnPair(
		AOutlierPlayerState* ShooterPlayerState,
		AOutlierPlayerState* PartnerPlayerState,
		int32 ArenaId,
		const FTransform& ShooterSpawn,
		const FTransform& PartnerSpawn);

	AOutlierPlayerState* FindPairPlayerState(int32 PairId, EOutlierPlayerRole PlayerRole) const;
	AController* GetControllerFromPlayerState(AOutlierPlayerState* PlayerState) const;
	void ApplyCheckpointToPair(AOutlierPlayerState* TriggeringPlayerState, const FOutlierCheckpointData& Data);
	void RegisterSpawnedPair(AOutlierPlayerState* ShooterPlayerState, AOutlierPlayerState* PartnerPlayerState, AShooterCharacter* Shooter, APartnerCharacter* Partner);
	//APlayerController* SwapPlayerController(APlayerController* OldPC, TSubclassOf<APlayerController> NewClass);

	bool ResolveArenaSpawnTransforms(
		int32 ArenaId,
		FTransform& OutShooterSpawn,
		FTransform& OutPartnerSpawn) const;

	// APresetPlayerStart를 못 찾았을 때의 스폰 위치 폴백. 아레나의 일반 PlayerStart →
	// 엔진 FindPlayerStart → 원점 순으로 내려가며, 항상 무언가를 채워준다.
	// 프리셋을 못 찾았다고 리스폰 자체를 포기하면 플레이어가 죽은 채로 방치되므로,
	// 최초 진입/디버그 리로드/프리셋 리스폰 세 경로가 같은 폴백을 공유한다.
	// FindPlayerStart가 non-const라 이 함수도 non-const다.
	void ResolveFallbackSpawnTransforms(
		AController* Requester,
		int32 ArenaId,
		FTransform& OutShooterSpawn,
		FTransform& OutPartnerSpawn);

private:
	bool IsArenaWorkerProcess() const;
	bool UsesStaticArenaHandoff() const;
	void PauseArenaWorkerWorld();
	void ClearArenaWorkerWorldPause();
	void ScheduleArenaWorkerPairSetup();
	bool HandleArenaWorkerPairSetupTick(float DeltaTime);
	void TryStartArenaWorkerPair();
	void ScheduleArenaWorkerGameplayStart();
	bool HandleArenaWorkerGameplayStartTick(float DeltaTime);
	void StartArenaWorkerGameplay();
	void PossessMatchedPawn(APlayerController* PlayerController, APawn* Pawn, int32 ArenaId, const FVector& SpawnLocation);
	void TryScheduleArenaWorkerAutoComplete();
	void HandleArenaWorkerAutoComplete();
	void RequestArenaWorkerExit();

	TArray<TWeakObjectPtr<APlayerController>> ArenaWorkerPlayers;
	FOutlierArenaAdmissionState ArenaWorkerAdmission;
	TWeakObjectPtr<APlayerController> ArenaWorkerShooterController;
	TWeakObjectPtr<APlayerController> ArenaWorkerPartnerController;
	TSet<TWeakObjectPtr<APlayerController>> ArenaWorkerReadyPlayers;
	UPROPERTY(Transient)
	TObjectPtr<AOutlierArenaPausePlayerState> ArenaWorkerPauseOwner;
	UPROPERTY(Transient)
	TObjectPtr<UWorldPartitionStreamingSourceComponent> ArenaWorkerPreloadSource;
	bool bArenaWorkerPairStartScheduled = false;
	bool bArenaWorkerPairStarted = false;
	bool bArenaWorkerGameplayStartScheduled = false;
	bool bArenaWorkerGameplayStarted = false;
	bool bArenaWorkerMatchCompleting = false;
	bool bArenaWorkerExitRequested = false;
	FTimerHandle ArenaWorkerAutoCompleteTimerHandle;
	FTimerHandle ArenaWorkerExitTimerHandle;
	FTSTicker::FDelegateHandle ArenaWorkerPairSetupTickerHandle;
	FTSTicker::FDelegateHandle ArenaWorkerGameplayStartTickerHandle;
};
