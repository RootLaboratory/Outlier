// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierLobbyIdentitySubsystem.generated.h"

/** */
UCLASS()
class OUTLIER_API UOutlierLobbyIdentitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	FGuid RegisterPlayer(APlayerController* PlayerController);
	void UnregisterPlayer(AController* Controller);

	bool TryGetPlayerId(const AController* Controller, FGuid& OutPlayerId) const;

	APlayerController* FindPlayer(const FGuid& PlayerId) const;

	bool RebindPlayer(const FGuid& PlayerId, APlayerController* PlayerController);

private:
	TMap<FGuid, TWeakObjectPtr<APlayerController>> ActivePlayers;
};
