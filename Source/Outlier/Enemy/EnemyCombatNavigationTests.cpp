#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemyCombatNavigation.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatNavigationTimingTest,
	"Outlier.Enemy.Combat.Navigation.AttackTiming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatNavigationTimingTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCombatNavigation;
	TestEqual(TEXT("Acceleration followed by cruise takes three seconds"),
		EstimateAttackSeconds(1000, 0, 500, 250, 0, 240, 0, 0, 0, true), 3.0);
	TestEqual(TEXT("Preparation and recovery overlap movement instead of being summed"),
		EstimateAttackSeconds(1000, 500, 500, 250, 90, 90, 2.0, 4.0, 0.2, true), 4.0);
	TestEqual(TEXT("Hidden preparation cannot be consumed during two seconds of travel"),
		EstimateAttackSeconds(1000, 500, 500, 250, 0, 240, 0.5, 0, 0, false), 2.5);
	TestEqual(TEXT("Pure acceleration solves a short route before reaching max speed"),
		EstimateAttackSeconds(125, 0, 500, 250, 0, 240, 0, 0, 0, true), 1.0);
	TestEqual(TEXT("An immobile candidate is unavailable"),
		EstimateAttackSeconds(100, 0, 0, 0, 0, 240, 0, 0, 0, true), TNumericLimits<double>::Max());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatNavigationRankingTest,
	"Outlier.Enemy.Combat.Navigation.Ranking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatNavigationRankingTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCombatNavigation;
	TestTrue(TEXT("A comparable attack time preserves engagement distance"), IsBetterCandidate(1.05, 10, 1.0, 100));
	TestFalse(TEXT("A small speed gain cannot force a worse engagement distance"), IsBetterCandidate(0.95, 100, 1.0, 10));
	TestTrue(TEXT("A material speed gain takes precedence"), IsBetterCandidate(0.7, 100, 1.0, 10));
	TestFalse(TEXT("A valid route does not flip for a minor gain"), ShouldReplacePath(2.0, 1.8));
	TestTrue(TEXT("The approved gain permits changing route"), ShouldReplacePath(2.0, 1.7));
	TestTrue(TEXT("An invalid route can be replaced by a slower verified route"), ShouldReplacePath(1.0, 2.0, 0.3, false));
	TestFalse(TEXT("An unavailable route cannot replace a safe route"), ShouldReplacePath(1.0, TNumericLimits<double>::Max()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyCombatNavigationCandidatesTest,
	"Outlier.Enemy.Combat.Navigation.CandidateFamilies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCombatNavigationCandidatesTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCombatNavigation;
	TArray<FCandidate> Candidates;
	BuildCandidates(FVector(1000, 0, 200), FVector::ZeroVector, 1500, true, Candidates);
	TestTrue(TEXT("Mixed search has a fixed finite candidate bound"), Candidates.Num() > 0 && Candidates.Num() <= 24);
	bool bPositive = false, bNegative = false, bFurther = false, bProgress = false;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FCandidate& Candidate = Candidates[Index];
		bPositive |= !Candidate.bProgressOnly && Candidate.Destination.Y > 100;
		bNegative |= !Candidate.bProgressOnly && Candidate.Destination.Y < -100;
		bFurther |= !Candidate.bProgressOnly && FVector::Dist2D(Candidate.Destination, FVector::ZeroVector) > 1100;
		bProgress |= Candidate.bProgressOnly;
		TestEqual(TEXT("Candidate keeps the flight-height seed for subsequent physical validation"), Candidate.Destination.Z, 200.0);
		for (int32 Other = 0; Other < Index; ++Other)
		{
			TestFalse(TEXT("Candidate locations are unique across intentions"), Candidate.Destination.Equals(Candidates[Other].Destination, 1.0));
		}
	}
	TestTrue(TEXT("Both cover sides, retreat and explicit nonattack progress exist"), bPositive && bNegative && bFurther && bProgress);
	BuildCandidates(FVector::ZeroVector, FVector::ZeroVector, 0, true, Candidates);
	TestTrue(TEXT("An invalid weapon range never supplies movement candidates"), Candidates.IsEmpty());
	return true;
}

#endif
