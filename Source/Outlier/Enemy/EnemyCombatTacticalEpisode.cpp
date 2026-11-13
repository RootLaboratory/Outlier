#include "Enemy/EnemyCombatTacticalEpisode.h"

namespace
{
	constexpr int32 MaxFailureEntries = 64;
	constexpr int32 MaxCheckedEntries = 128;
	constexpr double FailureLifetimeSeconds = 30.0;
}

uint64 FEnemyCombatTacticalEpisode::Ensure(UObject* NewTarget, UObject* NewController,
	int32 NewGeneration, int32 NewLease, const FVector& NewGoal, const FVector& NewOrigin,
	float NewChangeDistance, const FEnemyStat& Stat)
{
	if (!IsValid(NewTarget) || !IsValid(NewController) || NewGoal.ContainsNaN() || NewOrigin.ContainsNaN()) return 0;
	const bool bOwnerChanged = Controller.Get() != NewController || Generation != NewGeneration || Lease != NewLease;
	if (bOwnerChanged) Reset(true);
	Controller = NewController;
	Generation = NewGeneration;
	Lease = NewLease;
	ChangeDistance = FMath::IsFinite(NewChangeDistance) ? FMath::Max(1.0f, NewChangeDistance) : 200.0f;
	MaxRequests = FMath::Max(1, Stat.TacticalMaxRequests);
	MaxRecoveryRequests = FMath::Max(1, Stat.TacticalRecoveryMaxRequests);
	MaxSeconds = FMath::IsFinite(Stat.TacticalMaxSeconds) ? FMath::Max(0.01f, Stat.TacticalMaxSeconds) : 3.0;
	MaxRecoverySeconds = FMath::IsFinite(Stat.TacticalRecoveryMaxSeconds)
		? FMath::Max(0.01f, Stat.TacticalRecoveryMaxSeconds) : 3.0;
	Origin = NewOrigin;
	// 기준 목표는 작은 갱신마다 이동시키지 않는다. 누적 이동이 기준 거리를 넘었을 때만 새 회차다.
	if (!bHasGoal || Target.Get() != NewTarget || FVector::DistSquared(Goal, NewGoal) > FMath::Square(ChangeDistance))
	{
		Target = NewTarget;
		Goal = NewGoal;
		bHasGoal = true;
		RestartGoal();
	}
	return Serial;
}

void FEnemyCombatTacticalEpisode::RestartGoal()
{
	++Serial;
	StartedAt = -1.0;
	Requests = 0;
	bObserving = bSawBudgetShortage = bBudgetRecoveryPending = bRecoveryUsed = bRecovering = false;
	bMovingNormally = false;
	CheckedCandidates.Reset();
	CurrentFailureBounds = FBox(ForceInit);
}

void FEnemyCombatTacticalEpisode::Reset(bool bClearFailures)
{
	RestartGoal();
	bHasGoal = false;
	Target.Reset();
	if (bClearFailures)
	{
		Failures.Reset();
		Controller.Reset();
		Generation = Lease = INDEX_NONE;
	}
}

bool FEnemyCombatTacticalEpisode::HasExpired(double Now) const
{
	return !bMovingNormally && StartedAt >= 0.0 && Now - StartedAt >= (bRecovering ? MaxRecoverySeconds : MaxSeconds);
}

bool FEnemyCombatTacticalEpisode::IsCurrent(uint64 ExpectedSerial, double Now) const
{
	return bHasGoal && Target.IsValid() && Controller.IsValid() && ExpectedSerial != 0
		&& ExpectedSerial == Serial && !bObserving && !HasExpired(Now);
}

bool FEnemyCombatTacticalEpisode::TryBeginRequest(double Now)
{
	if (!bHasGoal || bObserving || !FMath::IsFinite(Now)) return false;
	if (HasExpired(Now) || Requests >= (bRecovering ? MaxRecoveryRequests : MaxRequests))
	{
		EnterObservation(bSawBudgetShortage);
		return false;
	}
	if (StartedAt < 0.0) StartedAt = Now;
	++Requests;
	return true;
}

void FEnemyCombatTacticalEpisode::EnterObservation(bool bBudgetShortage)
{
	if (bObserving) return;
	// 먼저 번호를 폐기하므로 같은 lease의 늦은 성공도 이동/실패 기록을 덮어쓸 수 없다.
	++Serial;
	bObserving = true;
	bMovingNormally = false;
	bBudgetRecoveryPending = bBudgetShortage && bSawBudgetShortage && !bRecoveryUsed;
}

bool FEnemyCombatTacticalEpisode::CanResumeBudget() const
{
	return bHasGoal && bObserving && bBudgetRecoveryPending && !bRecoveryUsed;
}

bool FEnemyCombatTacticalEpisode::TryResumeBudget(bool bActualExecutionGranted, double Now)
{
	if (!bActualExecutionGranted || !CanResumeBudget() || !FMath::IsFinite(Now)) return false;
	++Serial;
	bObserving = bBudgetRecoveryPending = bSawBudgetShortage = false;
	bRecoveryUsed = bRecovering = true;
	Requests = 0;
	StartedAt = Now;
	// 예산 때문에 건너뛴 후보만 다시 허용한다. 실제 공간 실패 캐시는 복구 구간에도 유지한다.
	CheckedCandidates.Reset();
	return true;
}

void FEnemyCombatTacticalEpisode::CompleteSelectionSuccess(bool bInvalidatePendingRequests)
{
	if (bObserving) return;
	if (bInvalidatePendingRequests) ++Serial;
	// 검증된 정상 이동은 다음 선회를 가로막지 않는다. 소진된 실패 회차를 성공 콜백으로 되살리지는 않는다.
	StartedAt = -1.0;
	Requests = 0;
	bMovingNormally = false;
	CheckedCandidates.Reset();
	bSawBudgetShortage = false;
}

bool FEnemyCombatTacticalEpisode::Matches(const FCandidate& Entry, FName Intent, const FVector& Candidate) const
{
	return Entry.Intent == Intent && Entry.Position.Equals(Candidate, 1.0f);
}

void FEnemyCombatTacticalEpisode::PruneFailures(double Now)
{
	Failures.RemoveAll([Now](const FFailure& Entry)
	{
		return !Entry.Target.IsValid() || Now - Entry.RecordedAt >= FailureLifetimeSeconds;
	});
}

bool FEnemyCombatTacticalEpisode::IsFailure(FName Intent, const FVector& Candidate, double Now)
{
	PruneFailures(Now);
	return Failures.ContainsByPredicate([&](const FFailure& Entry)
	{
		return Matches(Entry, Intent, Candidate) && Entry.Target == Target
			&& Entry.Goal.Equals(Goal, ChangeDistance) && Entry.Origin.Equals(Origin, ChangeDistance);
	});
}

void FEnemyCombatTacticalEpisode::RecordFailure(FName Intent, const FVector& Candidate, const FBox& Bounds, double Now)
{
	if (!bHasGoal || bObserving || Candidate.ContainsNaN() || !Bounds.IsValid) return;
	PruneFailures(Now);
	if (IsFailure(Intent, Candidate, Now)) return;
	if (Failures.Num() >= MaxFailureEntries) Failures.RemoveAt(0, 1, EAllowShrinking::No);
	FFailure& Entry = Failures.AddDefaulted_GetRef();
	Entry.Intent = Intent;
	Entry.Position = Candidate;
	Entry.Target = Target;
	Entry.Goal = Goal;
	Entry.Origin = Origin;
	Entry.Bounds = Bounds;
	Entry.RecordedAt = Now;
	CurrentFailureBounds += Bounds;
}

bool FEnemyCombatTacticalEpisode::WasChecked(FName Intent, const FVector& Candidate) const
{
	return CheckedCandidates.ContainsByPredicate([&](const FCandidate& Entry) { return Matches(Entry, Intent, Candidate); });
}

void FEnemyCombatTacticalEpisode::MarkChecked(FName Intent, const FVector& Candidate)
{
	if (!bHasGoal || bObserving || Candidate.ContainsNaN() || WasChecked(Intent, Candidate)) return;
	if (CheckedCandidates.Num() >= MaxCheckedEntries) return;
	CheckedCandidates.Add({Intent, Candidate});
}

bool FEnemyCombatTacticalEpisode::InvalidateSpace(const FBox& ChangedBounds)
{
	if (!ChangedBounds.IsValid) return false;
	// 캐시 TTL은 증거 보관 상한이다. 현재 소진 회차의 관련 공간은 별도로 남겨 문이 열린 때 재평가한다.
	bool bAffectsCurrentGoal = CurrentFailureBounds.IsValid && CurrentFailureBounds.Intersect(ChangedBounds);
	Failures.RemoveAll([&](const FFailure& Entry)
	{
		if (!Entry.Bounds.Intersect(ChangedBounds)) return false;
		bAffectsCurrentGoal |= Entry.Target == Target && Entry.Goal.Equals(Goal, ChangeDistance);
		return true;
	});
	if (bAffectsCurrentGoal) RestartGoal();
	return bAffectsCurrentGoal;
}
