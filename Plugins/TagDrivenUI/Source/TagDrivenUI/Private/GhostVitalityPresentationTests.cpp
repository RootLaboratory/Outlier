#if WITH_DEV_AUTOMATION_TESTS

#include "GhostProgressBar.h"
#include "HPBarUI.h"
#include "PartnerHealthUI.h"
#include "LocalPlayerUISubSystem.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostVitalityHudTest, "Outlier.UI.GhostProgressBar.HudPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostVitalityHudTest::RunTest(const FString& Parameters)
{
	UHPBarUI* Hud = NewObject<UHPBarUI>();
	Hud->bDebugHPBarUI = false;
	UGhostProgressBar* Health = NewObject<UGhostProgressBar>(Hud);
	UGhostProgressBar* Shield = NewObject<UGhostProgressBar>(Hud);
	Hud->HPBar = Health;
	Hud->ShieldBar = Shield;
	Hud->HPText = NewObject<UTextBlock>(Hud);
	Hud->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Hud->SetHealthState(100.0f, 100.0f);
	Hud->SetShieldState(100.0f, 100.0f);
	Hud->SetHealthState(40.0f, 100.0f);
	TestEqual(TEXT("Main and number immediately reflect damage"), Health->GetPercent(), 0.4f);
	TestEqual(TEXT("Number has the actual value"), Hud->HPText->GetText().ToString(), FText::AsNumber(40).ToString());
	TestEqual(TEXT("Health keeps the previous fill"), Health->GetGhostPercent(), 1.0f);
	Health->AdvanceGhostAnimation(0.125f);
	Hud->SetHealthState(40.0f, 100.0f);
	Health->AdvanceGhostAnimation(0.425f);
	TestTrue(TEXT("Duplicate HUD events do not restart the delay"), FMath::IsNearlyEqual(Health->GetGhostPercent(), 0.475f, 1.e-5f));
	Hud->SetShieldState(60.0f, 100.0f, true);
	TestEqual(TEXT("Overcharge policy synchronizes the affected shield"), Shield->GetGhostPercent(), 0.6f);
	TestTrue(TEXT("Shield immediate policy does not cancel health animation"), Health->IsGhostAnimating());
	Hud->SetShieldState(30.0f, 100.0f);
	TestTrue(TEXT("Real hit after overcharge still animates"), Shield->IsGhostAnimating());
	Hud->SetHealthState(40.0f, 200.0f);
	TestEqual(TEXT("Maximum change synchronizes both fills"), Health->GetGhostPercent(), 0.2f);
	Hud->SetHealthState(std::numeric_limits<float>::infinity(), 200.0f);
	TestEqual(TEXT("Invalid state clears the bar immediately"), Health->GetGhostPercent(), 0.0f);
	TestFalse(TEXT("Invalid state leaves no scheduled animation"), Health->IsGhostAnimating());
	Hud->SetShieldState(10.0f, 100.0f);
	Hud->Deactivate();
	TestFalse(TEXT("Closing HUD discards trails"), Shield->IsGhostAnimating());

	UPartnerHealthUI* Partner = NewObject<UPartnerHealthUI>();
	Partner->HPBar = NewObject<UGhostProgressBar>(Partner);
	Partner->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Partner->SetHealthState(100.0f, 100.0f);
	Partner->SetHealthState(40.0f, 100.0f);
	UGhostProgressBar* PartnerHealth = CastChecked<UGhostProgressBar>(Partner->HPBar);
	TestTrue(TEXT("Partner HP uses the common damage policy"), PartnerHealth->IsGhostAnimating());
	Partner->SetHealthState(40.0f, 100.0f, true);
	TestFalse(TEXT("Partner restoration clears the trail even with the same value"), PartnerHealth->IsGhostAnimating());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhostVitalityStreamTest, "Outlier.UI.GhostProgressBar.OrderedStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhostVitalityStreamTest::RunTest(const FString& Parameters)
{
	ULocalPlayer* Player = NewObject<ULocalPlayer>();
	ULocalPlayerUISubSystem* UI = NewObject<ULocalPlayerUISubSystem>(Player);
	FOutlierVitalityPresentationUpdate Update;
	Update.Source = GetMutableDefault<AActor>();
	Update.Current = 100.0f;
	Update.Maximum = 100.0f;
	Update.Generation = 1;
	Update.Sequence = 1;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Delta without an initial snapshot is rejected"), UI->VitalityReceivedMask, uint8(0));
	Update.bSnapshot = true;
	UI->ApplyVitalityPresentation(Update);
	Update.bSnapshot = false;
	Update.Sequence = 2;
	Update.Current = 60.0f;
	Update.Policy = EOutlierVitalityUpdatePolicy::Immediate;
	UI->ApplyVitalityPresentation(Update);
	Update.Sequence = 3;
	Update.Current = 40.0f;
	Update.Policy = EOutlierVitalityUpdatePolicy::Animated;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Later hit preserves its own policy"), UI->CachedVitality[0].Policy, EOutlierVitalityUpdatePolicy::Animated);
	UI->OnRep_HealthChanged(90.0f, 100.0f);
	TestEqual(TEXT("Raw OnRep cannot overwrite the server stream"), UI->CachedVitality[0].Current, 40.0f);
	Update.Sequence = 2;
	Update.Current = 60.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Old sequence is ignored"), UI->CachedVitality[0].Current, 40.0f);
	Update.Generation = 2;
	Update.Sequence = 1;
	Update.Current = 100.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("New generation requires a snapshot"), UI->CachedVitality[0].Generation, uint32(1));
	Update.bSnapshot = true;
	UI->ApplyVitalityPresentation(Update);
	Update.Generation = 1;
	Update.Sequence = 99;
	Update.Current = 10.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Old pawn update cannot overwrite new pawn"), UI->CachedVitality[0].Current, 100.0f);
	Update.Generation = 3;
	Update.Sequence = 1;
	Update.Source = nullptr;
	Update.Current = Update.Maximum = 0.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Source removal clears the cached value"), UI->CachedVitality[0].Current, 0.0f);
	UI->ResetVitalityPresentation();
	TestEqual(TEXT("World teardown discards the stream"), UI->VitalityStreamMask, uint8(0));

	// PIE 재현: HP Snapshot이 Actor보다 먼저 도착한 뒤 같은 세대의 참조가 해석된다.
	Update.Channel = EOutlierVitalityChannel::PartnerHealth;
	Update.Generation = 2;
	Update.Sequence = 1;
	Update.Source = nullptr;
	Update.Current = Update.Maximum = 100.0f;
	Update.bSnapshot = true;
	UI->ApplyVitalityPresentation(Update);
	Update.Source = GetMutableDefault<AActor>();
	Update.Sequence = 2;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Resolved snapshot adopts the source in the same generation"),
		UI->CachedVitality[2].Source.Get(), Update.Source.Get());
	Update.bSnapshot = false;
	Update.Sequence = 3;
	Update.Current = 80.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Damage after resolving the source updates Partner HP"), UI->CachedVitality[2].Current, 80.0f);
	Update.Source = GetMutableDefault<APawn>();
	Update.Sequence = 4;
	Update.Current = 20.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("A different known source in the same generation is still rejected"), UI->CachedVitality[2].Current, 80.0f);

	// 별도 재요청 Snapshot이 없어도 최초 유효 Delta에서 참조를 확정할 수 있어야 한다.
	UI->ResetVitalityPresentation();
	Update.Source = nullptr;
	Update.Sequence = 1;
	Update.Current = 100.0f;
	Update.bSnapshot = true;
	UI->ApplyVitalityPresentation(Update);
	Update.bSnapshot = false;
	Update.Sequence = 2;
	Update.Current = 80.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("An unresolved delta is still rejected"), UI->CachedVitality[2].Current, 100.0f);
	Update.Source = GetMutableDefault<AActor>();
	Update.Sequence = 3;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("First resolved delta after the snapshot updates Partner HP"), UI->CachedVitality[2].Current, 80.0f);
	Update.Sequence = 2;
	Update.Current = 60.0f;
	UI->ApplyVitalityPresentation(Update);
	TestEqual(TEXT("Source adoption preserves sequence validation"), UI->CachedVitality[2].Current, 80.0f);
	return true;
}

#endif
