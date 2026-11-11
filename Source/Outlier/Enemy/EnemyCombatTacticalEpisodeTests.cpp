#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyCombatTacticalEpisode.h"
#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticalEpisodeLimitsTest,
	"Outlier.Enemy.Combat.TacticalEpisode.Limits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticalEpisodeLimitsTest::RunTest(const FString& Parameters)
{
	UObject* Target = NewObject<USceneComponent>();
	UObject* Controller = NewObject<USceneComponent>();
	FEnemyStat Stat;
	FEnemyCombatTacticalEpisode Episode;
	Episode.Ensure(Target, Controller, 1, 1, FVector(1000, 0, 0), FVector::ZeroVector, 200, Stat);
	const uint64 Initial = Episode.GetSerial();
	TestTrue(TEXT("First request starts the common clock"), Episode.TryBeginRequest(10));
	TestTrue(TEXT("Changing movement task consumes the same second request"), Episode.TryBeginRequest(10.5));
	TestTrue(TEXT("Third request remains permitted"), Episode.TryBeginRequest(11));
	TestFalse(TEXT("Fourth request cannot mint another intent budget"), Episode.TryBeginRequest(11.5));
	TestTrue(TEXT("Request exhaustion enters observation"), Episode.IsObserving());
	TestFalse(TEXT("Entering observation rejects the old same-lease serial"), Episode.IsCurrent(Initial, 11.5));
	Episode.Ensure(Target, Controller, 1, 1, FVector(1050, 0, 0), FVector::ZeroVector, 200, Stat);
	TestFalse(TEXT("Small target refresh never resumes an exhausted episode"), Episode.TryBeginRequest(12));
	Episode.CompleteSelectionSuccess();
	TestTrue(TEXT("A late success cannot release the exhausted guard"), Episode.IsObserving());
	Episode.Ensure(Target, Controller, 1, 1, FVector(1250, 0, 0), FVector::ZeroVector, 200, Stat);
	TestTrue(TEXT("Meaningful adopted goal change opens a new episode"), Episode.TryBeginRequest(13));
	TestFalse(TEXT("Wall time expires even without another request"), Episode.IsCurrent(Episode.GetSerial(), 16));
	TestFalse(TEXT("Wall deadline cannot be extended by a retry"), Episode.TryBeginRequest(16));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticalEpisodeMovementTest,
	"Outlier.Enemy.Combat.TacticalEpisode.ValidatedMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticalEpisodeMovementTest::RunTest(const FString& Parameters)
{
	FEnemyCombatTacticalEpisode Episode;
	FEnemyStat Stat;
	UObject* Target = NewObject<USceneComponent>();
	UObject* Controller = NewObject<USceneComponent>();
	Episode.Ensure(Target, Controller, 1, 1, FVector(1000, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(0);
	Episode.BeginMovement(false);
	TestTrue(TEXT("Validated normal approach is allowed beyond the selection deadline"), Episode.IsCurrent(Episode.GetSerial(), 10));
	Episode.EndMovement();
	TestFalse(TEXT("Movement failure exposes the original deadline without resetting it"), Episode.TryBeginRequest(10));
	Episode.Ensure(Target, Controller, 1, 1, FVector(1300, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(20);
	Episode.BeginMovement(true);
	TestFalse(TEXT("Sight reposition travel keeps the absolute three-second limit"), Episode.IsCurrent(Episode.GetSerial(), 23));
	Episode.Reset(false);
	Episode.Ensure(Target, Controller, 1, 1, FVector(1300, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(30);
	Episode.BeginMovement(false);
	Episode.CompleteSelectionSuccess();
	Episode.EndMovement();
	TestTrue(TEXT("Successful arrival allows the next normal orbit query"), Episode.TryBeginRequest(50));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticalEpisodeRecoveryTest,
	"Outlier.Enemy.Combat.TacticalEpisode.BudgetRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticalEpisodeRecoveryTest::RunTest(const FString& Parameters)
{
	FEnemyCombatTacticalEpisode Episode;
	FEnemyStat Stat;
	UObject* Target = NewObject<USceneComponent>();
	Episode.Ensure(Target, NewObject<USceneComponent>(), 1, 1, FVector(1000, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(0);
	Episode.MarkChecked(TEXT("Approach"), FVector(100, 0, 0));
	Episode.RecordFailure(TEXT("Retreat"), FVector(200, 0, 0), FBox(FVector(-100), FVector(300)), 0);
	Episode.NoteBudgetShortage();
	Episode.EnterObservation(true);
	TestTrue(TEXT("Budget observation is eligible for a funded recovery"), Episode.CanResumeBudget());
	TestFalse(TEXT("Polling without an actual grant cannot open recovery"), Episode.TryResumeBudget(false, 9));
	TestTrue(TEXT("Actual execution grant opens the one recovery interval"), Episode.TryResumeBudget(true, 10));
	TestFalse(TEXT("A funded interval can retry temporary budget candidates"), Episode.WasChecked(TEXT("Approach"), FVector(100, 0, 0)));
	TestTrue(TEXT("Funded recovery retains unexpired physical evidence"), Episode.IsFailure(TEXT("Retreat"), FVector(200, 0, 0), 10));
	TestTrue(TEXT("First recovery request is permitted"), Episode.TryBeginRequest(10));
	TestTrue(TEXT("Second recovery request is permitted"), Episode.TryBeginRequest(10.5));
	TestFalse(TEXT("Recovery has its separate two-request cap"), Episode.TryBeginRequest(11));
	Episode.NoteBudgetShortage();
	Episode.EnterObservation(true);
	TestFalse(TEXT("Repeated availability never grants another recovery"), Episode.TryResumeBudget(true, 200));
	Episode.Reset(false);
	Episode.Ensure(Target, NewObject<USceneComponent>(), 1, 1, FVector(1000, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.EnterObservation(false);
	TestFalse(TEXT("Configuration/physical failures are not budget recovery"), Episode.TryResumeBudget(true, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticalEpisodeCacheTest,
	"Outlier.Enemy.Combat.TacticalEpisode.FailureSpace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticalEpisodeCacheTest::RunTest(const FString& Parameters)
{
	FEnemyCombatTacticalEpisode Episode;
	FEnemyStat Stat;
	UObject* A = NewObject<USceneComponent>();
	UObject* B = NewObject<USceneComponent>();
	UObject* Controller = NewObject<USceneComponent>();
	const FVector Goal(1000, 0, 0), Candidate(400, 0, 0);
	const FBox Bounds(FVector(-100, -100, -100), FVector(500, 100, 100));
	Episode.Ensure(A, Controller, 1, 1, Goal, FVector::ZeroVector, 200, Stat);
	Episode.RecordFailure(TEXT("Retreat"), Candidate, Bounds, 0);
	Episode.MarkChecked(TEXT("Approach"), Candidate);
	TestTrue(TEXT("Checked cursor belongs to the common episode"), Episode.WasChecked(TEXT("Approach"), Candidate));
	TestFalse(TEXT("A checked candidate is not a physical failure"), Episode.IsFailure(TEXT("Approach"), Candidate, 0));
	Episode.Ensure(B, Controller, 1, 1, Goal, FVector::ZeroVector, 200, Stat);
	TestFalse(TEXT("A failure never applies to another target"), Episode.IsFailure(TEXT("Retreat"), Candidate, 1));
	Episode.Ensure(A, Controller, 1, 1, Goal, FVector::ZeroVector, 200, Stat);
	TestTrue(TEXT("A to B to A retains the physical failure"), Episode.IsFailure(TEXT("Retreat"), Candidate, 2));
	TestFalse(TEXT("Unrelated door does not restart the episode"), Episode.InvalidateSpace(FBox(FVector(9000), FVector(9100))));
	TestTrue(TEXT("Related door invalidates affected failure space"), Episode.InvalidateSpace(Bounds));
	TestFalse(TEXT("Relevant space change releases the failed candidate"), Episode.IsFailure(TEXT("Retreat"), Candidate, 3));
	Episode.RecordFailure(TEXT("Retreat"), Candidate, Bounds, 4);
	Episode.TryBeginRequest(4);
	Episode.EnterObservation(false);
	TestFalse(TEXT("Expired evidence is evicted"), Episode.IsFailure(TEXT("Retreat"), Candidate, 100));
	TestTrue(TEXT("TTL eviction never restarts a spent episode"), Episode.IsObserving());
	Episode.Ensure(A, Controller, 1, 2, Goal, FVector::ZeroVector, 200, Stat);
	TestFalse(TEXT("Another pool lease never inherits old failure evidence"), Episode.IsFailure(TEXT("Retreat"), Candidate, 100));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticalEpisodeCompletionTest,
	"Outlier.Enemy.Combat.TacticalEpisode.ReopenedPathCompletesOnlyLiveEpisode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticalEpisodeCompletionTest::RunTest(const FString& Parameters)
{
	FEnemyCombatTacticalEpisode Episode;
	FEnemyStat Stat;
	UObject* Target = NewObject<USceneComponent>();
	UObject* Controller = NewObject<USceneComponent>();
	Episode.Ensure(Target, Controller, 1, 1, FVector(1000, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(0);
	const uint64 BlockedSerial = Episode.GetSerial();
	Episode.CompleteSelectionSuccess();
	TestFalse(TEXT("Reopened path invalidates the old blocked request even if cover returns"),
		Episode.IsCurrent(BlockedSerial, 0.5));
	TestTrue(TEXT("A successful reopening does not later expire at the former deadline"),
		Episode.IsCurrent(Episode.GetSerial(), 4));
	TestTrue(TEXT("A later obstruction may start a bounded live episode"), Episode.TryBeginRequest(4));
	Episode.EnterObservation(false);
	const uint64 SpentSerial = Episode.GetSerial();
	Episode.CompleteSelectionSuccess();
	TestTrue(TEXT("An already exhausted episode remains observing when fire becomes possible"), Episode.IsObserving());
	TestEqual(TEXT("A late successful result cannot create a fresh spent-episode serial"), Episode.GetSerial(), SpentSerial);
	Episode.Ensure(Target, Controller, 1, 1, FVector(1500, 0, 0), FVector::ZeroVector, 200, Stat);
	Episode.TryBeginRequest(5);
	Episode.BeginMovement(false);
	const uint64 ArrivalSerial = Episode.GetSerial();
	Episode.CompleteSelectionSuccess(false);
	TestTrue(TEXT("Normal arrival preserves the sibling destination guard through the stationary hold handoff"),
		Episode.IsCurrent(ArrivalSerial, 10));
	return true;
}

#endif
