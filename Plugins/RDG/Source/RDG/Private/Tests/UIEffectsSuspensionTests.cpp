#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/LocalPlayer.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRDGUIEffectsSuspensionTest,
	"Outlier.RDG.UIEffectsSuspension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRDGUIEffectsSuspensionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>();
	ULocalPlayerPostProcessSubsystem* PostProcess =
		NewObject<ULocalPlayerPostProcessSubsystem>(LocalPlayer);
	PostProcess->SetChromaticAberrationEnabled(true);
	PostProcess->SetPixelSortingEnabled(true);
	PostProcess->SetZoomBlurEnabled(true);
	PostProcess->SetDatamoshingEnabled(true);
	TestTrue(TEXT("Lens distortion is initially visible"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);

	PostProcess->SetUIEffectsSuspended(true);
	TestFalse(TEXT("Menu suppresses lens distortion"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);
	TestFalse(TEXT("Menu suppresses backbuffer pixel sorting"),
		PostProcess->GetPostProcessStrcture().PixelSorting.bEnabled != 0);
	TestFalse(TEXT("Menu suppresses backbuffer zoom blur"),
		PostProcess->GetPostProcessStrcture().ZoomBlur.bEnabled != 0);
	TestTrue(TEXT("Scene-only datamoshing stays enabled"),
		PostProcess->GetPostProcessStrcture().Datamoshing.bEnabled != 0);

	PostProcess->SetChromaticAberrationStartOffset(0.25f);
	PostProcess->SetChromaticAberrationEnabled(true);
	TestFalse(TEXT("Effect updates cannot re-enable distortion while a menu remains open"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);
	PostProcess->SetUIEffectsSuspended(false);
	TestTrue(TEXT("Closing the menu restores the requested effect"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);
	TestEqual(TEXT("Changes made during suspension are preserved"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.StartOffset, 0.25f);
	TestTrue(TEXT("Closing the menu restores pixel sorting"),
		PostProcess->GetPostProcessStrcture().PixelSorting.bEnabled != 0);

	PostProcess->SetUIEffectsSuspended(true);
	PostProcess->StartGameOverBlackout();
	TestTrue(TEXT("Menu suspension preserves the respawn blackout pass"),
		PostProcess->GetPostProcessStrcture().ZoomBlur.bEnabled != 0);
	TestEqual(TEXT("Respawn blackout remains fully covered"),
		PostProcess->GetPostProcessStrcture().ZoomBlur.BlackFlushAlpha, 1.0f);
	PostProcess->ResetAllPostProcess();
	TestTrue(TEXT("An effect reset does not release a menu's suspension"),
		PostProcess->AreUIEffectsSuspended());
	PostProcess->SetChromaticAberrationEnabled(true);
	TestFalse(TEXT("After blackout ends an open menu still suppresses lens distortion"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);
	PostProcess->SetUIEffectsSuspended(false);
	TestTrue(TEXT("Normal output resumes when the last menu closes"),
		PostProcess->GetUIPostProcessStrcture().ChromaticAberration.bEnabled != 0);
	return true;
}

#endif
