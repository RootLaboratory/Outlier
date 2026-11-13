#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "UI/TargetPointHudWidget.h"
#include "Components/CanvasPanel.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractableComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetPointHudLayoutTest,
	"Outlier.UI.TargetPointHud.LayoutAndVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTargetPointHudLayoutTest::RunTest(const FString& Parameters)
{
	FTargetPointHudSettings Settings;
	TestEqual(TEXT("Reference distance has normal size"), Settings.GetDistanceScale(200), 1.0f);
	TestEqual(TEXT("Near distance is capped"), Settings.GetDistanceScale(1), 1.5f);
	TestEqual(TEXT("Far distance stays readable"), Settings.GetDistanceScale(10000), 0.5f);
	TestTrue(TEXT("Perspective shrinks with distance"), Settings.GetDistanceScale(160) > Settings.GetDistanceScale(320));
	FVector2D Position;
	float Scale;
	Settings.CalculateLayout(FVector2D(500, 400), FVector2D(1000, 800), FVector2D(100, 40), 200, Position, Scale);
	TestEqual(TEXT("Unclamped widget centers on target"), Position, FVector2D(450, 380));
	Settings.CalculateLayout(FVector2D(500, 400), FVector2D(1000, 800), FVector2D(100, 40),
		200, Position, Scale, FVector2D(0, 1));
	TestEqual(TEXT("WorldGuide pivot anchors the bottom left corner"), Position, FVector2D(500, 360));
	Settings.CalculateLayout(FVector2D(2000, -200), FVector2D(1000, 800), FVector2D(100, 40),
		200, Position, Scale, FVector2D(0, 1));
	TestEqual(TEXT("WorldGuide pivot still keeps the full widget within screen padding"), Position, FVector2D(884, 16));
	for (const FVector2D Point : { FVector2D(-200, -200), FVector2D(2000, 2000), FVector2D(0, 800) })
	{
		Settings.CalculateLayout(Point, FVector2D(1000, 800), FVector2D(400, 200), 1, Position, Scale);
		TestTrue(TEXT("Entire scaled widget stays inside horizontal padding"), Position.X >= 16 && Position.X + 400 * Scale <= 984.01);
		TestTrue(TEXT("Entire scaled widget stays inside vertical padding"), Position.Y >= 16 && Position.Y + 200 * Scale <= 784.01);
	}
	Settings.CalculateLayout(FVector2D(20), FVector2D(64), FVector2D(400, 200), 1, Position, Scale);
	TestTrue(TEXT("Small screen fitting overrides minimum scale"), Scale < Settings.MinScale && Scale > 0);
	Settings.MinScale = 2;
	Settings.MaxScale = 1;
	TestEqual(TEXT("Reversed limits are safe"), Settings.GetDistanceScale(200), 2.0f);
	TestFalse(TEXT("No controller cannot project"), Settings.Project(nullptr, FVector::ZeroVector, FVector2D(64), Position, Scale));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Widget fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot(); };
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	ON_SCOPE_EXIT { Controller->Player = nullptr; Player->PlayerController = nullptr; };
	UTargetPointHudWidget* Hud = CreateWidget<UTargetPointHudWidget>(Controller);
	UTargetPointWidget* Content = CreateWidget<UTargetPointWidget>(Controller);
	if (!TestNotNull(TEXT("HUD host initializes canvas"), Hud) || !TestNotNull(TEXT("Content"), Content)) return false;
	Hud->AddContent(Content);
	TestTrue(TEXT("Content belongs to HUD canvas"), Content->GetParent() && Content->GetParent()->IsA<UCanvasPanel>());
	Hud->UpdateContent(Content, true, FVector::ZeroVector, FVector2D(64));
	TestEqual(TEXT("Unprojectable content is hidden"), Content->GetVisibility(), ESlateVisibility::Collapsed);
	Hud->UpdateContent(Content, false, FVector::ZeroVector, FVector2D(64));
	TestEqual(TEXT("Dismissed content is hidden"), Content->GetVisibility(), ESlateVisibility::Collapsed);
	Hud->Release();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetPointWidgetSettingsTest,
	"Outlier.UI.TargetPointHud.WidgetOwnedSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTargetPointWidgetSettingsTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("Actors no longer expose per-target HUD settings"),
		FindFProperty<FProperty>(UInteractableComponent::StaticClass(), TEXT("HudPresentation")));
	const FProperty* SettingsProperty = FindFProperty<FProperty>(UTargetPointWidget::StaticClass(), TEXT("HudPresentation"));
	if (!TestNotNull(TEXT("Widget defaults own presentation settings"), SettingsProperty)) return false;
	TestTrue(TEXT("Widget instances cannot override class defaults in Details"),
		SettingsProperty->HasAllPropertyFlags(CPF_Edit | CPF_DisableEditOnInstance));
	// 실제 위젯 BP를 로드해 기존 부모 클래스와 새 공통 설정이 함께 유지되는지 확인한다.
	for (const TCHAR* Name : { TEXT("WBP_InteractionInfo"), TEXT("WBP_InteractionKey"),
		TEXT("WBP_InteractionLockedWidget"), TEXT("WBP_UInteractionOutlineWidget"), TEXT("WBP_InteractionDescWidget") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Blueprints/Widget/FirstPerson/Interaction/%s.%s_C"), Name, Name);
		UClass* WidgetClass = LoadClass<UTargetPointWidget>(nullptr, *Path);
		if (!TestNotNull(Path, WidgetClass)) continue;
		const UTargetPointWidget* Defaults = WidgetClass->GetDefaultObject<UTargetPointWidget>();
		TestEqual(Path + TEXT(" near limit"), Defaults->HudPresentation.GetDistanceScale(1), Defaults->HudPresentation.MaxScale);
		TestEqual(Path + TEXT(" far limit"), Defaults->HudPresentation.GetDistanceScale(100000), Defaults->HudPresentation.MinScale);
	}
	return true;
}
#endif
