#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyStat.h"

// StateTree 자식/의도가 바뀌어도 유지하는 서버 전술 회차다. 월드 실행 예산은 소유하지 않는다.
struct OUTLIER_API FEnemyCombatTacticalEpisode
{
	uint64 Ensure(UObject* Target, UObject* Controller, int32 Generation, int32 Lease,
		const FVector& Goal, const FVector& Origin, float ChangeDistance, const FEnemyStat& Stat);
	void Reset(bool bClearFailures);
	uint64 GetSerial() const { return bHasGoal ? Serial : 0; }
	bool IsCurrent(uint64 ExpectedSerial, double Now) const;
	bool IsObserving() const { return bObserving; }
	bool HasExpired(double Now) const;
	bool TryBeginRequest(double Now);
	void EnterObservation(bool bBudgetShortage);
	void NoteBudgetShortage() { bSawBudgetShortage = true; }
	bool CanResumeBudget() const;
	bool TryResumeBudget(bool bActualExecutionGranted, double Now);
	void CompleteSelectionSuccess(bool bInvalidatePendingRequests = true);
	void BeginMovement(bool bSightReposition) { bMovingNormally = !bSightReposition && !bObserving; }
	void EndMovement() { bMovingNormally = false; }
	bool IsFailure(FName Intent, const FVector& Candidate, double Now);
	void RecordFailure(FName Intent, const FVector& Candidate, const FBox& Bounds, double Now);
	bool WasChecked(FName Intent, const FVector& Candidate) const;
	void MarkChecked(FName Intent, const FVector& Candidate);
	bool InvalidateSpace(const FBox& ChangedBounds);
	const FBox& GetCurrentFailureBounds() const { return CurrentFailureBounds; }

private:
	struct FCandidate
	{
		FName Intent;
		FVector Position = FVector::ZeroVector;
	};
	struct FFailure : FCandidate
	{
		TWeakObjectPtr<UObject> Target;
		FVector Goal = FVector::ZeroVector;
		FVector Origin = FVector::ZeroVector;
		FBox Bounds = FBox(ForceInit);
		double RecordedAt = 0.0;
	};
	void RestartGoal();
	bool Matches(const FCandidate& Entry, FName Intent, const FVector& Candidate) const;
	void PruneFailures(double Now);

	TWeakObjectPtr<UObject> Target;
	TWeakObjectPtr<UObject> Controller;
	int32 Generation = INDEX_NONE;
	int32 Lease = INDEX_NONE;
	uint64 Serial = 1;
	FVector Goal = FVector::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	float ChangeDistance = 200.0f;
	int32 MaxRequests = 3;
	int32 MaxRecoveryRequests = 2;
	double MaxSeconds = 3.0;
	double MaxRecoverySeconds = 3.0;
	double StartedAt = -1.0;
	int32 Requests = 0;
	bool bHasGoal = false;
	bool bObserving = false;
	bool bSawBudgetShortage = false;
	bool bBudgetRecoveryPending = false;
	bool bRecoveryUsed = false;
	bool bRecovering = false;
	bool bMovingNormally = false;
	TArray<FCandidate> CheckedCandidates;
	TArray<FFailure> Failures;
	FBox CurrentFailureBounds = FBox(ForceInit);
};
