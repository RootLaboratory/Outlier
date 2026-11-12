#if WITH_DEV_AUTOMATION_TESTS

#include "GhostProgressBar.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostProgressBarTimingTest, "Outlier.UI.GhostProgressBar.Timing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostProgressBarTimingTest::RunTest(const FString& Parameters)
{
	UGhostProgressBar* Bar = NewObject<UGhostProgressBar>();
	Bar->SetTargetPercent(0.4f);
	TestEqual(TEXT("First real value synchronizes both fills"), Bar->GetGhostPercent(), 0.4f);
	TestFalse(TEXT("First value does not create an artificial damage trail"), Bar->IsGhostAnimating());
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.6f);
	TestEqual(TEXT("Main drops immediately"), Bar->GetPercent(), 0.6f);
	Bar->AdvanceGhostAnimation(0.125f);
	TestEqual(TEXT("Ghost remains fixed during delay"), Bar->GetGhostPercent(), 1.0f);
	Bar->SetTargetPercent(0.6f);
	Bar->AdvanceGhostAnimation(0.125f);
	TestEqual(TEXT("Same target did not restart delay"), Bar->GetGhostPercent(), 1.0f);
	Bar->AdvanceGhostAnimation(0.3f);
	TestTrue(TEXT("Half duration uses cubic ease weight 0.875"), FMath::IsNearlyEqual(Bar->GetGhostPercent(), 0.65f, 1.e-5f));
	Bar->AdvanceGhostAnimation(0.31f);
	TestEqual(TEXT("Completion snaps exactly to target"), Bar->GetGhostPercent(), 0.6f);
	TestFalse(TEXT("Completion becomes idle"), Bar->IsGhostAnimating());

	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.2f);
	Bar->AdvanceGhostAnimation(0.55f);
	TestTrue(TEXT("A long frame carries the unused delay time into interpolation"), FMath::IsNearlyEqual(Bar->GetGhostPercent(), 0.3f, 1.e-5f));
	UGhostProgressBar* SplitFrameBar = NewObject<UGhostProgressBar>();
	SplitFrameBar->SetPercentImmediate(1.0f);
	SplitFrameBar->SetTargetPercent(0.2f);
	for (int32 Index = 0; Index < 11; ++Index) SplitFrameBar->AdvanceGhostAnimation(0.05f);
	TestTrue(TEXT("Frame splitting does not change the trajectory"), FMath::IsNearlyEqual(Bar->GetGhostPercent(), SplitFrameBar->GetGhostPercent(), 1.e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostProgressBarRestartTest, "Outlier.UI.GhostProgressBar.RestartAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostProgressBarRestartTest::RunTest(const FString& Parameters)
{
	UGhostProgressBar* Bar = NewObject<UGhostProgressBar>();
	Bar->SetGhostTiming(0.25f, 1.0f);
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.6f);
	Bar->AdvanceGhostAnimation(0.75f);
	const float DisplayedGhost = Bar->GetGhostPercent();
	Bar->SetTargetPercent(0.4f);
	TestEqual(TEXT("Repeated damage starts at the displayed ghost"), Bar->GetGhostPercent(), DisplayedGhost);
	Bar->AdvanceGhostAnimation(0.125f);
	TestEqual(TEXT("Repeated damage gets a fresh delay"), Bar->GetGhostPercent(), DisplayedGhost);
	Bar->SetTargetPercent(0.6f);
	TestEqual(TEXT("Recovery updates actual value immediately"), Bar->GetPercent(), 0.6f);
	TestEqual(TEXT("Recovery clears damage trail to displayed main"), Bar->GetGhostPercent(), 0.4f);
	Bar->AdvanceGhostAnimation(0.25f);
	TestTrue(TEXT("Recovery starts without damage delay"), Bar->GetDisplayedMainPercent() > 0.4f);
	TestEqual(TEXT("Recovery fills stay together"), Bar->GetGhostPercent(), Bar->GetDisplayedMainPercent());
	Bar->AdvanceGhostAnimation(1.0f);
	TestEqual(TEXT("Remaining recovery trail reaches the new main"), Bar->GetGhostPercent(), 0.6f);
	Bar->SetTargetPercent(0.4f);
	Bar->SetTargetPercent(0.9f);
	TestEqual(TEXT("Recovery ghost starts with the main"), Bar->GetGhostPercent(), 0.4f);
	TestEqual(TEXT("Recovery main starts at its displayed value"), Bar->GetDisplayedMainPercent(), 0.4f);
	TestTrue(TEXT("Recovery above ghost animates the main fill"), Bar->IsGhostAnimating());
	Bar->AdvanceGhostAnimation(0.75f);
	TestTrue(TEXT("Recovery main uses cubic easing without delay"), FMath::IsNearlyEqual(Bar->GetDisplayedMainPercent(), 0.8921875f, 1.e-5f));
	TestEqual(TEXT("Both recovery fills use the same easing"), Bar->GetGhostPercent(), Bar->GetDisplayedMainPercent());
	Bar->AdvanceGhostAnimation(0.5f);
	TestEqual(TEXT("Recovery main reaches the target"), Bar->GetDisplayedMainPercent(), 0.9f);
	TestFalse(TEXT("Recovery completion becomes idle"), Bar->IsGhostAnimating());
	Bar->SetTargetPercent(0.2f);
	Bar->SetPercentImmediate(0.2f);
	TestEqual(TEXT("Immediate update clears trail even for the same target"), Bar->GetGhostPercent(), 0.2f);
	TestFalse(TEXT("Immediate update cancels pending delay"), Bar->IsGhostAnimating());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostProgressBarContinuousRecoveryTest, "Outlier.UI.GhostProgressBar.ContinuousRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostProgressBarContinuousRecoveryTest::RunTest(const FString& Parameters)
{
	UGhostProgressBar* Bar = NewObject<UGhostProgressBar>();
	Bar->SetPercentImmediate(0.0f);
	for (int32 Step = 1; Step <= 10; ++Step)
	{
		const float Before = Bar->GetDisplayedMainPercent();
		Bar->SetTargetPercent(Step * 0.1f);
		TestEqual(TEXT("Repeated recovery preserves displayed main"), Bar->GetDisplayedMainPercent(), Before);
		TestEqual(TEXT("Repeated recovery preserves synchronized fills"), Bar->GetGhostPercent(), Before);
		Bar->AdvanceGhostAnimation(0.1f);
		TestTrue(TEXT("Recovery main never moves backward"), Bar->GetDisplayedMainPercent() >= Before);
		TestEqual(TEXT("Continuous recovery animates both fills together"), Bar->GetGhostPercent(), Bar->GetDisplayedMainPercent());
		if (Step >= 3) TestTrue(TEXT("Frequent recovery does not perpetually restart delay"), Bar->GetDisplayedMainPercent() > Before);
	}
	Bar->AdvanceGhostAnimation(1.0f);
	TestEqual(TEXT("Continuous recovery finishes at full"), Bar->GetDisplayedMainPercent(), 1.0f);
	Bar->SetPercentImmediate(0.0f);
	Bar->SetTargetPercent(1.0f);
	Bar->AdvanceGhostAnimation(0.4f);
	const float BeforeDamage = Bar->GetGhostPercent();
	Bar->SetTargetPercent(0.5f);
	TestEqual(TEXT("Damage interrupts recovery immediately"), Bar->GetDisplayedMainPercent(), 0.5f);
	TestEqual(TEXT("Damage preserves the visible ghost"), Bar->GetGhostPercent(), BeforeDamage);
	Bar->AdvanceGhostAnimation(1.0f);
	TestEqual(TEXT("Damage after recovery finishes its trail"), Bar->GetGhostPercent(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostProgressBarEdgeCasesTest, "Outlier.UI.GhostProgressBar.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostProgressBarEdgeCasesTest::RunTest(const FString& Parameters)
{
	UGhostProgressBar* Bar = NewObject<UGhostProgressBar>();
	Bar->SetGhostTiming(0.25f, 0.0f);
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.5f);
	Bar->AdvanceGhostAnimation(0.2f);
	TestEqual(TEXT("Zero duration still respects delay"), Bar->GetGhostPercent(), 1.0f);
	Bar->AdvanceGhostAnimation(0.1f);
	TestEqual(TEXT("Zero duration snaps when delay expires"), Bar->GetGhostPercent(), 0.5f);
	Bar->SetGhostTiming(0.0f, 0.0f);
	Bar->SetTargetPercent(0.2f);
	TestEqual(TEXT("Zero delay and duration are immediate"), Bar->GetGhostPercent(), 0.2f);
	TestFalse(TEXT("Zero timings do not schedule animation"), Bar->IsGhostAnimating());
	Bar->SetGhostTiming(0.0f, 1.0f);
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.0f);
	Bar->AdvanceGhostAnimation(0.5f);
	TestTrue(TEXT("Zero delay begins interpolation immediately"), FMath::IsNearlyEqual(Bar->GetGhostPercent(), 0.125f, 1.e-5f));
	const float BeforeInvalidDelta = Bar->GetGhostPercent();
	Bar->AdvanceGhostAnimation(-1.0f);
	Bar->AdvanceGhostAnimation(std::numeric_limits<float>::quiet_NaN());
	TestEqual(TEXT("Invalid elapsed time does not change presentation"), Bar->GetGhostPercent(), BeforeInvalidDelta);
	Bar->SetGhostEnabled(false);
	TestEqual(TEXT("Disabling clears old trail"), Bar->GetGhostPercent(), Bar->GetPercent());
	Bar->SetGhostEnabled(true);
	TestFalse(TEXT("Re-enabling does not resurrect animation"), Bar->IsGhostAnimating());
	Bar->SetTargetPercent(2.0f);
	TestEqual(TEXT("Ratio above one is clamped"), Bar->GetPercent(), 1.0f);
	Bar->SetTargetPercent(std::numeric_limits<float>::infinity());
	TestEqual(TEXT("Nonfinite ratio is immediately cleared"), Bar->GetGhostPercent(), 0.0f);
	TestFalse(TEXT("Nonfinite ratio leaves no animation"), Bar->IsGhostAnimating());
	AddExpectedError(TEXT("Invalid Ghost timing"), EAutomationExpectedErrorFlags::Contains, 1);
	Bar->SetGhostTiming(std::numeric_limits<float>::quiet_NaN(), -1.0f);
	Bar->SetGhostTiming(std::numeric_limits<float>::infinity(), -1.0f);
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.0f);
	Bar->AdvanceGhostAnimation(0.3f);
	TestEqual(TEXT("Invalid delay uses fallback and negative duration clamps to zero"), Bar->GetGhostPercent(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostProgressBarSlateLifecycleTest, "Outlier.UI.GhostProgressBar.SlateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostProgressBarSlateLifecycleTest::RunTest(const FString& Parameters)
{
	UGhostProgressBar* Bar = NewObject<UGhostProgressBar>();
	Bar->TakeWidget();
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.4f);
	Bar->AdvanceGhostAnimation(0.4f);
	Bar->ReleaseSlateResources(true);
	TestFalse(TEXT("Slate release cancels animation"), Bar->IsGhostAnimating());
	TestEqual(TEXT("Slate release discards old trail"), Bar->GetGhostPercent(), 0.4f);
	Bar->TakeWidget();
	Bar->SetTargetPercent(0.2f);
	TestEqual(TEXT("First value after rebuild is immediate"), Bar->GetGhostPercent(), 0.2f);
	TestFalse(TEXT("Rebuild does not replay old damage"), Bar->IsGhostAnimating());
	Bar->SetPercentImmediate(1.0f);
	Bar->SetTargetPercent(0.5f);
	Bar->SetVisibility(ESlateVisibility::Collapsed);
	TestFalse(TEXT("Direct hiding clears pending animation"), Bar->IsGhostAnimating());
	Bar->SetVisibility(ESlateVisibility::Visible);
	Bar->SetTargetPercent(0.2f);
	Bar->RemoveFromParent();
	TestFalse(TEXT("Removal clears pending animation"), Bar->IsGhostAnimating());
	Bar->ReleaseSlateResources(true);
	return true;
}

#endif
