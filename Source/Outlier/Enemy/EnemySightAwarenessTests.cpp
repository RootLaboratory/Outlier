#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/EnemySightAwareness.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessSequenceTest,
	"Outlier.Enemy.SightAwareness.SequenceAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessSequenceTest::RunTest(const FString& Parameters)
{
	FEnemySightAwareness State;
	TestTrue(TEXT("A new observation starts successfully"), State.Reset(10.0, true));
	TestFalse(TEXT("0.6 seconds does not complete discovery"), State.Observe(10.6, false));
	TestTrue(TEXT("Visible 0.6 seconds fills 60%"), FMath::IsNearlyEqual(State.GetAwareness(), 0.6));
	TestFalse(TEXT("Hiding does not complete discovery"), State.Observe(11.0, true));
	TestTrue(TEXT("Hidden 0.4 seconds leaves 40%"), FMath::IsNearlyEqual(State.GetAwareness(), 0.4));
	TestTrue(TEXT("Another 0.6 seconds completes discovery"), State.Observe(11.6, true));
	TestEqual(TEXT("Completion is exactly 100%"), State.GetAwareness(), 1.0);
	TestTrue(TEXT("Completion is latched"), State.IsComplete());
	TestFalse(TEXT("The same timestamp does not complete twice"), State.Observe(11.6, true));
	TestFalse(TEXT("Later observations do not complete twice"), State.Observe(100.0, false));
	TestEqual(TEXT("A completed lifetime stays complete"), State.GetAwareness(), 1.0);
	State.Reset(100.0, true);
	TestFalse(TEXT("Reset clears completion"), State.IsComplete());
	TestEqual(TEXT("Reset clears progress"), State.GetAwareness(), 0.0);
	TestTrue(TEXT("A new lifetime may complete again"), State.Observe(101.0, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessDurationTest,
	"Outlier.Enemy.SightAwareness.ConfigurableDurations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessDurationTest::RunTest(const FString& Parameters)
{
	for (const double DecayDuration : { 2.0, 5.0 })
	{
		FEnemySightAwareness State;
		State.Reset(0.0, true);
		State.Observe(0.6, false, 1.0, DecayDuration);
		const double HalfwayTime = 0.6 + 0.3 * DecayDuration;
		State.Observe(HalfwayTime, false, 1.0, DecayDuration);
		TestTrue(TEXT("Half the remaining decay time leaves 30%"), FMath::IsNearlyEqual(State.GetAwareness(), 0.3));
		State.Observe(0.6 + 0.6 * DecayDuration, false, 1.0, DecayDuration);
		TestEqual(TEXT("60% decays in 60% of the configured duration"), State.GetAwareness(), 0.0);
		State.Observe(100.0, true, 1.0, DecayDuration);
		TestEqual(TEXT("Long concealment never goes below zero"), State.GetAwareness(), 0.0);
	}
	FEnemySightAwareness SlowFill;
	SlowFill.Reset(0.0, true);
	SlowFill.Observe(1.0, true, 2.0);
	TestEqual(TEXT("Two-second fill reaches 50% after one second"), SlowFill.GetAwareness(), 0.5);
	TestTrue(TEXT("Large elapsed time clamps to completion"), SlowFill.Observe(100.0, true, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessInvalidDurationTest,
	"Outlier.Enemy.SightAwareness.InvalidDurations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessInvalidDurationTest::RunTest(const FString& Parameters)
{
	const double InvalidDurations[] = { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
	AddExpectedError(TEXT("Invalid awareness fill duration"), EAutomationExpectedErrorFlags::Contains, 5);
	AddExpectedError(TEXT("Invalid awareness decay duration"), EAutomationExpectedErrorFlags::Contains, 5);
	for (const double InvalidDuration : InvalidDurations)
	{
		FEnemySightAwareness State;
		State.Reset(0.0, true);
		State.Observe(0.6, false, InvalidDuration);
		TestTrue(TEXT("Invalid fill falls back to one second"), FMath::IsNearlyEqual(State.GetAwareness(), 0.6));
		State.Observe(1.0, true, 1.0, InvalidDuration);
		TestTrue(TEXT("Invalid decay falls back to two seconds"), FMath::IsNearlyEqual(State.GetAwareness(), 0.4));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessTimelineTest,
	"Outlier.Enemy.SightAwareness.FrameRatesAndObservationBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessTimelineTest::RunTest(const FString& Parameters)
{
	for (const int32 Rate : { 15, 30, 60 })
	{
		FEnemySightAwareness State;
		State.Reset(0.0, true);
		int32 CompletionCount = 0;
		// 상태 변경을 구간 끝에 보고한다. 마지막 호출이 다음 구간의 가시 상태를 등록한다.
		for (int32 Frame = 1; Frame <= Rate * 3 / 5; ++Frame)
		{
			CompletionCount += State.Observe(static_cast<double>(Frame) / Rate, Frame != Rate * 3 / 5);
		}
		TestTrue(TEXT("All frame rates reach 60%"), FMath::IsNearlyEqual(State.GetAwareness(), 0.6));
		for (int32 Frame = 1; Frame <= Rate * 2 / 5; ++Frame)
		{
			CompletionCount += State.Observe(0.6 + static_cast<double>(Frame) / Rate, Frame == Rate * 2 / 5);
		}
		TestTrue(TEXT("All frame rates retain 40%"), FMath::IsNearlyEqual(State.GetAwareness(), 0.4));
		for (int32 Frame = 1; Frame <= Rate * 3 / 5; ++Frame)
		{
			CompletionCount += State.Observe(1.0 + static_cast<double>(Frame) / Rate, true);
		}
		TestEqual(TEXT("All frame rates complete exactly once"), CompletionCount, 1);
		TestEqual(TEXT("All frame rates reach exactly 100%"), State.GetAwareness(), 1.0);
	}

	FEnemySightAwareness State;
	TestFalse(TEXT("First observation does not infer past exposure"), State.Observe(100.0, true));
	TestEqual(TEXT("No retroactive progress"), State.GetAwareness(), 0.0);
	State.Observe(100.4, false);
	for (int32 Query = 0; Query < 100; ++Query)
	{
		State.Observe(100.4, false);
		TestTrue(TEXT("Repeated queries do not advance progress"), FMath::IsNearlyEqual(State.GetAwareness(), 0.4));
	}
	State.Observe(101.0, true);
	TestTrue(TEXT("A hidden segment in a large interval is subtracted"), FMath::IsNearlyEqual(State.GetAwareness(), 0.1));
	State.Observe(101.0, false);
	State.Observe(101.2, true);
	TestEqual(TEXT("Same-time state changes apply to the next interval"), State.GetAwareness(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessInvalidTimeTest,
	"Outlier.Enemy.SightAwareness.InvalidTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessInvalidTimeTest::RunTest(const FString& Parameters)
{
	FEnemySightAwareness State;
	State.Reset(10.0, true);
	State.Observe(10.4, true);
	AddExpectedError(TEXT("Invalid awareness observation time"), EAutomationExpectedErrorFlags::Contains, 5);
	TestFalse(TEXT("Backward observation is rejected"), State.Observe(10.3, false));
	TestFalse(TEXT("NaN observation is rejected"), State.Observe(std::numeric_limits<double>::quiet_NaN(), false));
	TestFalse(TEXT("Infinite observation is rejected"), State.Observe(std::numeric_limits<double>::infinity(), false));
	TestFalse(TEXT("Invalid reset is rejected"), State.Reset(std::numeric_limits<double>::quiet_NaN(), false));
	TestTrue(TEXT("Invalid observations preserve progress"), FMath::IsNearlyEqual(State.GetAwareness(), 0.4));
	State.Observe(10.6, false);
	TestTrue(TEXT("Invalid observations preserve the previous visibility and time"), FMath::IsNearlyEqual(State.GetAwareness(), 0.6));
	State.Reset(-std::numeric_limits<double>::max(), true);
	TestFalse(TEXT("Overflowing elapsed time is rejected"), State.Observe(std::numeric_limits<double>::max(), true));
	TestEqual(TEXT("Overflow does not corrupt progress"), State.GetAwareness(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessExposureTest,
	"Outlier.Enemy.SightAwareness.ExposureThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessExposureTest::RunTest(const FString& Parameters)
{
	for (int32 RegisteredCount = 1; RegisteredCount <= 10; ++RegisteredCount)
	{
		for (int32 VisibleCount = 0; VisibleCount <= RegisteredCount; ++VisibleCount)
		{
			TestEqual(TEXT("At least half the registered bones are required"),
				OutlierEnemySight::MeetsExposureThreshold(RegisteredCount, VisibleCount),
				static_cast<double>(VisibleCount) / RegisteredCount >= 0.5);
		}
	}
	TestFalse(TEXT("Empty registration is not visible"), OutlierEnemySight::MeetsExposureThreshold(0, 0));
	TestFalse(TEXT("Negative registration is invalid"), OutlierEnemySight::MeetsExposureThreshold(-1, 0));
	TestFalse(TEXT("Negative visible count is invalid"), OutlierEnemySight::MeetsExposureThreshold(5, -1));
	TestFalse(TEXT("Visible count cannot exceed registration"), OutlierEnemySight::MeetsExposureThreshold(5, 6));
	TestTrue(TEXT("Threshold does not overflow at the integer limit"),
		OutlierEnemySight::MeetsExposureThreshold(MAX_int32, MAX_int32 / 2 + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySightAwarenessIsolationTest,
	"Outlier.Enemy.SightAwareness.IndependentStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemySightAwarenessIsolationTest::RunTest(const FString& Parameters)
{
	FEnemySightAwareness First;
	FEnemySightAwareness Second;
	First.Reset(0.0, true);
	Second.Reset(0.0, false);
	First.Observe(0.6, false);
	Second.Observe(0.6, true);
	TestTrue(TEXT("First state has its own progress"), FMath::IsNearlyEqual(First.GetAwareness(), 0.6));
	TestEqual(TEXT("Second state does not inherit progress"), Second.GetAwareness(), 0.0);
	Second.Observe(0.8, true);
	First.Reset(0.8, false);
	TestTrue(TEXT("Resetting another state does not clear progress"), FMath::IsNearlyEqual(Second.GetAwareness(), 0.2));
	TestFalse(TEXT("Completion remains independent"), Second.IsComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAwarenessHearingTimelineTest,
	"Outlier.Enemy.SightAwareness.HearingTimeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAwarenessHearingTimelineTest::RunTest(const FString& Parameters)
{
	FEnemySightAwareness State;
	State.Observe(10.0, false, 1.0, 2.0, 10.5);
	State.Observe(10.5, false, 1.0, 2.0, 10.5);
	TestEqual(TEXT("One sound supplies half a second at 25% speed"), State.GetAwareness(), 0.125);
	TestFalse(TEXT("Hearing ends exactly at its deadline"), State.IsHearingActive(10.5));
	State.Observe(10.75, false);
	TestEqual(TEXT("A single sound then decays to zero"), State.GetAwareness(), 0.0);

	State.Reset(0.0, true);
	State.Observe(0.6, false);
	State.Observe(0.6, false, 1.0, 2.0, 1.1);
	State.Observe(1.3, false, 1.0, 2.0, 1.1);
	TestTrue(TEXT("A large interval splits hearing gain and later decay"), FMath::IsNearlyEqual(State.GetAwareness(), 0.625));
	State.Reset(0.0, true);
	State.Observe(0.0, true, 1.0, 2.0, 0.5);
	State.Observe(0.5, false, 1.0, 2.0, 1.0);
	TestEqual(TEXT("Simultaneous sight and hearing use sight speed"), State.GetAwareness(), 0.5);
	State.Observe(1.0, false, 1.0, 2.0, 1.0);
	TestEqual(TEXT("Hearing continues the same progress after sight loss"), State.GetAwareness(), 0.625);
	for (int32 Query = 0; Query < 100; ++Query) State.Observe(1.0, false, 1.0, 2.0, 1.0);
	TestEqual(TEXT("Duplicate observations do not multiply hearing gain"), State.GetAwareness(), 0.625);

	for (const int32 Rate : { 15, 30, 60 })
	{
		State.Reset(0.0, false);
		State.Observe(0.0, false, 1.0, 2.0, 0.5);
		int32 Completions = 0;
		for (int32 Frame = 1; Frame <= Rate * 4; ++Frame)
		{
			const double Now = static_cast<double>(Frame) / Rate;
			Completions += State.Observe(Now, false, 1.0, 2.0, Now + 0.5);
		}
		TestEqual(TEXT("Continuous hearing completes once after four seconds at every frame rate"), Completions, 1);
		TestEqual(TEXT("Continuous hearing reaches 100%"), State.GetAwareness(), 1.0);
	}
	State.Reset(0.0, true);
	State.Observe(0.9, false);
	State.Observe(0.9, false, 1.0, 2.0, 1.4);
	TestTrue(TEXT("Completion inside hearing interval is not erased by later concealment in the same update"), State.Observe(2.0, false));
	TestTrue(TEXT("Hearing completion keeps its source across later decay"), State.WasCompletedFromHearing());
	TestFalse(TEXT("Completion is not emitted twice"), State.Observe(3.0, false));
	State.Reset(0.0, true);
	State.Observe(0.8, true, 1.0, 2.0, 1.3);
	State.Observe(0.99, false, 1.0, 2.0, 1.3);
	State.Observe(1.04, false, 1.0, 2.0, 1.3);
	TestTrue(TEXT("Hearing completes even when sight was observed after the last sound"), State.WasCompletedFromHearing());
	State.Reset(0.0, true);
	State.Observe(0.0, true, 1.0, 2.0, 0.5);
	State.Observe(1.0, false);
	TestFalse(TEXT("A sight-filled interval stays a sight completion after visibility changes"), State.WasCompletedFromHearing());

	State.Reset(10.0, false);
	State.Observe(10.0, false, 1.0, 2.0, 9.9);
	State.Observe(10.5, false);
	TestEqual(TEXT("An already expired sound does not fill past time"), State.GetAwareness(), 0.0);
	State.Reset(0.0, false);
	State.Observe(0.0, false, 2.0, 4.0, 1.0, 0.5);
	State.Observe(1.0, false, 2.0, 4.0, 1.0, 0.5);
	TestEqual(TEXT("Hearing ratio scales the configured sight fill speed"), State.GetAwareness(), 0.25);

	AddExpectedError(TEXT("Invalid awareness hearing ratio"), EAutomationExpectedErrorFlags::Contains, 5);
	AddExpectedError(TEXT("Invalid awareness hearing hold duration"), EAutomationExpectedErrorFlags::Contains, 5);
	for (const double Invalid : { 0.0, -1.0, 2.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
	{
		State.Reset(0.0, false);
		State.Observe(0.0, false, 1.0, 2.0, 0.5);
		State.Observe(0.5, false, 1.0, 2.0, 0.5, Invalid);
		TestEqual(TEXT("Invalid ratio falls back to 25%"), State.GetAwareness(), 0.125);
		const double InvalidHold = Invalid == 2.0 ? -2.0 : Invalid;
		TestEqual(TEXT("Invalid hold falls back to half a second"), OutlierEnemySight::ResolveHearingAwarenessHoldDuration(InvalidHold), 0.5);
	}
	return true;
}

#endif
