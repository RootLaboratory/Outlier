// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OutlierGameMode.generated.h"

class AShooterCharacter;
class AOutlierCheckpoint;

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

	void RegisterCheckpoint(AShooterCharacter* Character, AOutlierCheckpoint* Checkpoint);

	UFUNCTION()
	void HandlePlayerDeath(AShooterCharacter* Character);

protected:
	virtual void Logout(AController* Exiting) override;

	void RespawnPlayerAtCheckpoint(AController* Controller);
	bool ResolveCheckpointTransform(AController* Controller, FTransform& OutTransform) const;
	FString GetPlayerSaveId(AController* Controller) const;

};



