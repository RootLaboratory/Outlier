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

	void RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint);
	void RefreshPairLinks(AOutlierPlayerState* TriggeringPlayerState);

	UFUNCTION()
	void HandlePlayerDeath(AShooterCharacter* Character);

	void StartMatchedPair(
		AController* FirstController,
		AController* SecondController,
		int32 PairId,
		int32 ArenaId,
		EOutlierPlayerRole FirstRole,
		EOutlierPlayerRole SecondRole);

	void OnClientArenaReady(APlayerController* PC);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Network|Arena")
	bool CompleteArenaMatch();

	// 디버그: 요청한 페어의 arena를 통째로 리로드하고 시작점에 재스폰
	void DebugReloadArena(AController* Requester);

private:
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<APawn>> PendingPossessions;

	// 디버그 리로드: 서버측 재스트리밍(OnArenaShown) 완료 후 possess할 로컬 PC들
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<APawn>> PendingLocalPossessions;

	void HandleServerArenaReloaded(int32 ReloadedArenaId);

	int32 ReloadingArenaId = INDEX_NONE;
	FDelegateHandle ArenaShownHandle;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	TSubclassOf<AShooterCharacter> ShooterClass;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	TSubclassOf<APartnerCharacter> PartnerClass;

	UPROPERTY(EditDefaultsOnly, Category = "Controller")
	TSubclassOf<AShooterPlayerController> ShooterControllerClass;

	UPROPERTY(EditDefaultsOnly, Category = "Controller")
	TSubclassOf<APartnerPlayerController> PartnerControllerClass;

protected:
	virtual void PreLogin(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
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

	AOutlierPlayerState* FindPairPlayerState(int32 PairId, EOutlierPlayerRole PlayerRole) const;
	AController* GetControllerFromPlayerState(AOutlierPlayerState* PlayerState) const;
	void ApplyCheckpointToPair(AOutlierPlayerState* TriggeringPlayerState, const FOutlierCheckpointData& Data);
	void RegisterSpawnedPair(AOutlierPlayerState* ShooterPlayerState, AOutlierPlayerState* PartnerPlayerState, AShooterCharacter* Shooter, APartnerCharacter* Partner);
	//APlayerController* SwapPlayerController(APlayerController* OldPC, TSubclassOf<APlayerController> NewClass);

	bool ResolveArenaSpawnTransforms(
		int32 ArenaId,
		FTransform& OutShooterSpawn,
		FTransform& OutPartnerSpawn) const;

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
	void PossessMatchedPawn(APlayerController* PlayerController, APawn* Pawn, int32 ArenaId);
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
