// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameMode.h"
#include "OutlierGameMode.generated.h"

class APlayerController;
class AShooterPlayerController;
class APartnerPlayerController;
class AShooterCharacter;
class APartnerCharacter;
class AOutlierCheckpoint;
class AOutlierPlayerState;
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

private:
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<APawn>> PendingPossessions;

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
	virtual void Logout(AController* Exiting) override;

	void RespawnPairAtCheckpoint(AController* Controller);
	bool ResolveCheckpointTransform(AController* Controller, FTransform& OutTransform) const;
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
};
