#include "DeveloperConvenienceWidgetTests.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "DamageFeedbackLayer.h"
#include "MainUIBase.h"

TFunction<void()> UDeveloperConvenienceTestWidget::OnInitialize;
TFunction<void()> UDeveloperConvenienceTestWidget::OnConstruct;

void UDeveloperConvenienceTestWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (OnInitialize) OnInitialize();
}

void UDeveloperConvenienceTestWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (OnConstruct) OnConstruct();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceWidgetLifecycleTest,
	"Outlier.UI.DeveloperConvenience.WidgetLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceWidgetLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Widget world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot(); };
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(Instance);
	auto* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
	auto* Viewport = NewObject<UDeveloperConvenienceTestViewport>(GEngine);
	Viewport->SetContext(World, Instance);
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	Player->PlayerAdded(Viewport, 0);
	ON_SCOPE_EXIT
	{
		UDeveloperConvenienceTestWidget::OnInitialize = nullptr;
		UDeveloperConvenienceTestWidget::OnConstruct = nullptr;
		Controller->ClearDeveloperConveniencePresentation();
		Player->PlayerRemoved();
		Controller->Player = nullptr;
		Player->PlayerController = nullptr;
	};
	Controller->DeveloperConvenienceWidgetClass = UDeveloperConvenienceTestWidget::StaticClass();
	auto* LayerSubsystem = Player->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	if (!TestNotNull(TEXT("Real local layer subsystem"), LayerSubsystem)) return false;
	{
		FDeveloperConvenienceSnapshot Open;
		Open.MatchId = FGuid::NewGuid();
		Open.SessionId = FGuid::NewGuid();
		Open.bIsOperator = true;
		Controller->SetDeveloperConvenienceStateFromServer(Open);
		auto* Widget = Controller->DeveloperConvenienceWidget.Get();
		if (!TestNotNull(TEXT("Convenience root widget"), Widget))
		{
			return false;
		}
		// NullRHI에는 실제 뷰포트가 없어 Slate 수명 종료가 발생한다. 표시 문맥을 주입해 페이지 입력을 격리한다.
		Widget->InitializeDeveloperConvenience(Open);
		TestTrue(TEXT("Operator can enter existing level module page"),
			Widget->SetDeveloperConveniencePage(EDeveloperConveniencePage::LevelSelection));
		TestTrue(TEXT("Escape is consumed by the submenu"), Controller->HandleDeveloperConvenienceKey(EKeys::Escape, false, false));
		TestEqual(TEXT("Escape returns to the main page"), Widget->GetDeveloperConveniencePage(), EDeveloperConveniencePage::MainMenu);
		TestEqual(TEXT("Submenu Escape preserves the pause session"), Controller->DeveloperConvenienceState.SessionId, Open.SessionId);
		TestTrue(TEXT("Submenu Escape preserves input blocking"), Controller->IsMoveInputIgnored());
		TestTrue(TEXT("Submenu Escape preserves root layer"), Controller->DeveloperConvenienceLayer.IsValid());
		Open.bIsOperator = false;
		Widget->InitializeDeveloperConvenience(Open);
		TestFalse(TEXT("Waiting player cannot navigate the operator page"),
			Widget->SetDeveloperConveniencePage(EDeveloperConveniencePage::LevelSelection));
		Open.bIsOperator = true;
		Open.bTransitioning = true;
		Widget->InitializeDeveloperConvenience(Open);
		TestFalse(TEXT("Accepted transition locks page changes"),
			Widget->SetDeveloperConveniencePage(EDeveloperConveniencePage::LevelSelection));
		Controller->SetDeveloperConvenienceStateFromServer(FDeveloperConvenienceSnapshot());
	}

	for (const bool bDuringConstruct : { false, true })
	{
		bool bCallbackRan = false;
		auto CloseSession = [&]()
		{
			bCallbackRan = true;
			FDeveloperConvenienceSnapshot Closed;
			Controller->SetDeveloperConvenienceStateFromServer(Closed);
		};
		UDeveloperConvenienceTestWidget::OnInitialize = bDuringConstruct ? TFunction<void()>() : CloseSession;
		UDeveloperConvenienceTestWidget::OnConstruct = bDuringConstruct ? TFunction<void()>(CloseSession) : TFunction<void()>();
		FDeveloperConvenienceSnapshot Open;
		Open.MatchId = FGuid::NewGuid();
		Open.SessionId = FGuid::NewGuid();
		Open.bIsOperator = true;
		Controller->SetDeveloperConvenienceStateFromServer(Open);
		TestTrue(bDuringConstruct ? TEXT("Construct closed session") : TEXT("Initialize closed session"), bCallbackRan);
		TestFalse(TEXT("Closed session leaves no menu layer"), LayerSubsystem->IsLayerActive(UILayerTags::GameMenu()));
		TestFalse(TEXT("Closed session leaves no presentation handle"), Controller->DeveloperConvenienceLayer.IsValid());
		TestFalse(TEXT("Closed session restores movement"), Controller->IsMoveInputIgnored());
		TestFalse(TEXT("Closed session restores look"), Controller->IsLookInputIgnored());
		Controller->ClearDeveloperConveniencePresentation();
		LayerSubsystem->ClearAllLayers();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceHitPresentationTest,
	"Outlier.UI.DeveloperConvenience.HitPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceHitPresentationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Hit presentation world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(Instance);
	auto* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
	auto* Viewport = NewObject<UDeveloperConvenienceTestViewport>(GEngine);
	Viewport->SetContext(World, Instance);
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	Player->PlayerAdded(Viewport, 0);
	ON_SCOPE_EXIT
	{
		Controller->ClearDeveloperHitPresentation();
		Player->PlayerRemoved();
		Controller->Player = nullptr;
		Player->PlayerController = nullptr;
	};
	UClass* FeedbackClass = LoadClass<UDamageFeedbackLayer>(nullptr,
		TEXT("/Game/Blueprints/Widget/FirstPerson/WBP_DamageFeedbackLayer.WBP_DamageFeedbackLayer_C"));
	if (!TestNotNull(TEXT("Existing damage feedback WBP"), FeedbackClass))
	{
		return false;
	}
	UMainUIBase* MainUI = NewObject<UMainUIBase>(Controller);
	MainUI->DamageFeedbackLayer = CreateWidget<UDamageFeedbackLayer>(Controller, FeedbackClass);
	MainUI->SetVisibility(ESlateVisibility::Collapsed);
	Controller->ShooterUIInstance = MainUI;
	APawn* FirstPawn = World->SpawnActor<APawn>();
	Controller->Possess(FirstPawn);
	Controller->ClientShowDeveloperConvenienceHit_Implementation(FirstPawn, FVector(100.0f, 0.0f, 0.0f));
	auto* LayerSubsystem = Player->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	UDamageFeedbackLayer* FirstWidget = Controller->DeveloperHitWidget;
	if (!TestNotNull(TEXT("Hit creates independent feedback"), FirstWidget))
	{
		return false;
	}
	TestEqual(TEXT("Main UI remains hidden during possession"), MainUI->GetVisibility(), ESlateVisibility::Collapsed);
	TestNotNull(TEXT("Feedback attaches to an independent layer"), FirstWidget->GetParent());
	TestTrue(TEXT("Feedback uses Gameplay layer"), LayerSubsystem->IsLayerActive(UILayerTags::Gameplay()));
	TestEqual(TEXT("Zero-health-damage hit shows directional feedback"), FirstWidget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("Feedback does not enable the cursor"), Controller->bShowMouseCursor);
	TestFalse(TEXT("Feedback does not block movement"), Controller->IsMoveInputIgnored());
	Controller->ClientShowDeveloperConvenienceHit_Implementation(FirstPawn, FVector(0.0f, 100.0f, 0.0f));
	TestEqual(TEXT("Repeated hits reuse the widget and pool"), Controller->DeveloperHitWidget.Get(), FirstWidget);
	APawn* NextPawn = World->SpawnActor<APawn>();
	Controller->Possess(NextPawn);
	TestNull(TEXT("Pawn replacement releases old feedback"), Controller->DeveloperHitWidget.Get());
	Controller->ClientShowDeveloperConvenienceHit_Implementation(FirstPawn, FVector(100.0f, 0.0f, 0.0f));
	TestNull(TEXT("Late old-pawn hit cannot recreate feedback"), Controller->DeveloperHitWidget.Get());
	Controller->ClientShowDeveloperConvenienceHit_Implementation(NextPawn, FVector(100.0f, 0.0f, 0.0f));
	TestNotNull(TEXT("New pawn can show fresh feedback"), Controller->DeveloperHitWidget.Get());
	Controller->bHudHiddenForDeath = true;
	Controller->RefreshHudVisibility();
	TestNull(TEXT("Death hides and releases independent feedback"), Controller->DeveloperHitWidget.Get());
	TestFalse(TEXT("Death removes the independent layer"), Controller->DeveloperHitLayer.IsValid());
	return true;
}
#endif
