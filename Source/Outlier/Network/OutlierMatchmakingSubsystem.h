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
	void CreateParty(AController* Controller);
	void JoinParty(AController* Controller, const FString& PartyCode);
	void LeaveParty(AController* Controller);
	bool SelectRoleInPendingMatch(AController* Controller, EOutlierPlayerRole DesiredRole);
	bool TryStartPendingMatch(AController* Controller);

	void Cancel(AController* Controller);
	void ReleaseMatch(int32 PairId);

	bool BuildMatchAssignment(
		AController* FirstController,
		AController* SecondController,
		EOutlierPlayerRole FirstRole,
		EOutlierPlayerRole SecondRole,
		FOutlierMatchAssignment& OutAssignment) const;

	bool TryGetMatchAssignment(
		const FGuid& MatchId,
		FOutlierMatchAssignment& OutAssignment) const;

private:
	friend class FOutlierMatchAssignmentLifecycleTest;
	friend class FOutlierPartyContractTest;
	friend class FOutlierPartyLifecycleContractTest;
	friend class FOutlierPartyJoinIntegrationTest;

	void TryCreateMatch();
	void TryCreatePairThenRolePickMatch();
	void TryCreateRoleQueueMatch();
	bool CreatePendingRolePickMatch(
		const FOutlierMatchRequest& First,
		const FOutlierMatchRequest& Second,
		EOutlierPendingMatchSource Source);
	FString GenerateUniquePartyCode() const;
	void NotifyPartyResult(
		AController* Controller,
		EOutlierPartyRequestResult Result,
		const FString& PartyCode = FString()) const;

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
	TMap<FString, FOutlierPendingParty> PendingPartiesByCode;

	UPROPERTY()
	TMap<int32, int32> ActivePairArenaIds;

	UPROPERTY()
	TMap<FGuid, FOutlierMatchAssignment> ActiveMatchAssignments;

	UPROPERTY()
	TMap<int32, FGuid> ActivePairMatchIds;

	EOutlierMatchmakingMode MatchmakingMode = EOutlierMatchmakingMode::PairThenRolePick;
	TMap<TWeakObjectPtr<AController>, double> LastPartyJoinAttemptTimes;

	int32 NextPendingMatchId = 1;
	int32 NextPairId = 1;
};
