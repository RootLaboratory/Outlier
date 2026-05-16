// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierMatchRequest.h"
#include "OutlierMatchmakingSubsystem.generated.h"

class AController;

UENUM(BlueprintType)
enum class EOutlierMatchmakingMode : uint8
{
	PairThenRolePick,
	RoleQueue
};

/**
 *
 */
UCLASS()
class OUTLIER_API UOutlierMatchmakingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void SetMatchmakingMode(EOutlierMatchmakingMode NewMode);

	void EnqueueForPairThenRolePick(AController* Controller);
	void EnqueueByRole(AController* Controller, EOutlierPlayerRole DesiredRole);
	bool SelectRoleInPendingMatch(AController* Controller, EOutlierPlayerRole DesiredRole);

	void Cancel(AController* Controller);
	void ReleaseMatch(int32 PairId);

private:
	void TryCreateMatch();
	void TryCreatePairThenRolePickMatch();
	void TryCreateRoleQueueMatch();

	void CreateMatch(
		AController* FirstController,
		AController* SecondController,
		EOutlierPlayerRole FirstRole,
		EOutlierPlayerRole SecondRole
	);

	UPROPERTY()
	TArray<FOutlierMatchRequest> WaitingPlayers;

	UPROPERTY()
	TArray<FOutlierPendingRolePickMatch> PendingRolePickMatches;

	UPROPERTY()
	TArray<FOutlierMatchRequest> WaitingShooters;

	UPROPERTY()
	TArray<FOutlierMatchRequest> WaitingPartners;

	UPROPERTY()
	TMap<int32, int32> ActivePairArenaIds;

	EOutlierMatchmakingMode MatchmakingMode = EOutlierMatchmakingMode::PairThenRolePick;

	int32 NextPendingMatchId = 1;
	int32 NextPairId = 1;
};
