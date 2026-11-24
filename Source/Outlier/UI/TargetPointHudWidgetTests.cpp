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

	FTargetPointHudSettings OffscreenSettings = Settings;
	OffscreenSettings.bKeepBehindCamera = true;
	const FVector2D ViewportSize(1920, 1080);
	const FVector2D ContentSize(100, 40);
	const FVector2D HeadPivot(0.5, 1.0);
	FVector2D PairPoint;
	FVector2D GuidePoint;
	bool bShowRearIndicator = false;
	TestTrue(TEXT("A directly rear pair marker resolves"), OffscreenSettings.ResolveWorldLayout(false,
		FVector2D::ZeroVector, FVector2D::ZeroVector, 0, 1, ViewportSize, ContentSize, 200, HeadPivot,
		PairPoint, Scale, &bShowRearIndicator));
	TestTrue(TEXT("A directly rear target shows the rear indicator"), bShowRearIndicator);
	TestTrue(TEXT("A directly rear guide marker resolves"), OffscreenSettings.ResolveWorldLayout(false,
		FVector2D::ZeroVector, FVector2D::ZeroVector, 0, 1, ViewportSize, ContentSize, 200, HeadPivot,
		GuidePoint, Scale, nullptr));
	TestEqual(TEXT("Pair and guide markers use the same rear point"), PairPoint, GuidePoint);
	TestEqual(TEXT("Directly behind sits inside the bottom padding"), PairPoint, FVector2D(910, 1024));

	for (const FVector2D Direction : { FVector2D(-1, -4), FVector2D(1, -4), FVector2D(-1, 4), FVector2D(1, 4) })
	{
		FVector2D NearRearPosition;
		OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, Direction, 1, 0.9,
			ViewportSize, ContentSize, 200, HeadPivot, NearRearPosition, Scale, &bShowRearIndicator);
		TestEqual(TEXT("Both sides of directly behind use bottom center"), NearRearPosition, PairPoint);
		TestTrue(TEXT("A directly rear target retains its rear indicator"), bShowRearIndicator);
	}

	for (const double RearAngle : { 14.0, 16.0 })
	{
		OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, FVector2D(1, 0), RearAngle, 0.9,
			ViewportSize, ContentSize, 200, HeadPivot, Position, Scale, &bShowRearIndicator);
		TestEqual(TEXT("The rear indicator keeps its 15 degree boundary"), bShowRearIndicator, RearAngle < 15.0);
	}

	for (const FVector2D Direction : { FVector2D(-1, -0.6), FVector2D(1, -0.6), FVector2D(-1, -4), FVector2D(1, 4) })
	{
		FVector2D Previous;
		OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, Direction, 60.001, 0.9,
			ViewportSize, ContentSize, 200, HeadPivot, Previous, Scale, nullptr);
		OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, Direction, 59.999, 0.9,
			ViewportSize, ContentSize, 200, HeadPivot, Position, Scale, nullptr);
		TestTrue(TEXT("The rear path starts at the projected edge point"), Position.Equals(Previous, 0.01));

		// 일정한 각도로 양쪽에서 정후방까지 돌면서 좌표와 표시 상태를 따로 검사한다.
		Previous = Position;
		for (double RearAngle = 59.75; RearAngle >= 0.0; RearAngle -= 0.25)
		{
			OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, Direction, RearAngle, 0.9,
				ViewportSize, ContentSize, 200, HeadPivot, Position, Scale, &bShowRearIndicator);
			TestTrue(TEXT("The marker does not reverse upward on its way to the bottom"), Position.Y >= Previous.Y - 0.001);
			TestTrue(TEXT("A small yaw step makes a bounded move along the edge"), FVector2D::Distance(Position, Previous) <= 20.0);
			TestTrue(TEXT("The widget stays on the padded perimeter"),
				FMath::IsNearlyEqual(Position.X, 16.0) || FMath::IsNearlyEqual(Position.X, 1804.0)
				|| FMath::IsNearlyEqual(Position.Y, 16.0) || FMath::IsNearlyEqual(Position.Y, 1024.0));
			TestEqual(TEXT("The rear indicator uses the existing threshold during the sweep"), bShowRearIndicator, RearAngle <= 15.0);
			Previous = Position;
		}
		TestEqual(TEXT("The path ends at bottom center from either side"), Position, PairPoint);
	}

	// 좌우 가장자리에서 같은 투영 방향으로 카메라 평면을 통과한다.
	for (const FVector2D Size : { ViewportSize, FVector2D(1280, 960), FVector2D(960, 540) })
	{
		for (const FVector2D Direction : { FVector2D(-1, -0.3), FVector2D(1, 0.25) })
		{
			const FVector2D FrontProjection = Size * 0.5 + Direction * 100000.0;
			OffscreenSettings.ResolveWorldLayout(true, FrontProjection, FVector2D::ZeroVector, 180, 0,
				Size, ContentSize, 200, HeadPivot, Position, Scale, nullptr);
			OffscreenSettings.ResolveWorldLayout(false, FVector2D::ZeroVector, Direction, 90, 0.0001,
				Size, ContentSize, 200, HeadPivot, GuidePoint, Scale, nullptr);
			TestTrue(TEXT("Projection success and failure meet at the same edge"), Position.Equals(GuidePoint, 0.01));
			TestTrue(TEXT("The whole edge marker fits vertically"), Position.Y >= 16.0
				&& Position.Y + ContentSize.Y * Scale <= Size.Y - 16.0);
			TestEqual(TEXT("The marker reaches the correct side"), Position.X,
				Direction.X < 0.0 ? 16.0 : Size.X - 16.0 - ContentSize.X * Scale);
		}
	}

	TestFalse(TEXT("Default content still hides when projection fails"), Settings.ResolveWorldLayout(false,
		FVector2D::ZeroVector, FVector2D(-1, 0), 90, 0.5, ViewportSize, ContentSize, 200, HeadPivot,
		Position, Scale, &bShowRearIndicator));
	TestTrue(TEXT("Default content still shows when a front target is offscreen"), Settings.ResolveWorldLayout(true,
		FVector2D(2000, 400), FVector2D::ZeroVector, 180, 0, ViewportSize, ContentSize, 200, HeadPivot,
		Position, Scale, &bShowRearIndicator));
	TestEqual(TEXT("The front offscreen icon sits inside the right padding"), Position.X, 1804.0);

	bShowRearIndicator = true;
	const FVector2D OnScreenPoint(940, 450);
	TestTrue(TEXT("The target resolves on screen after the rear marker"), OffscreenSettings.ResolveWorldLayout(true,
		OnScreenPoint, FVector2D::ZeroVector, 180, 0, ViewportSize, ContentSize, 200, HeadPivot,
		Position, Scale, &bShowRearIndicator));
	TestEqual(TEXT("Returning to screen uses the projected head point"), Position, FVector2D(890, 410));
	TestFalse(TEXT("Returning to screen clears the rear indicator"), bShowRearIndicator);
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
