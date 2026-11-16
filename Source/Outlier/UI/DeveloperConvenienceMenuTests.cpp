#if WITH_DEV_AUTOMATION_TESTS

#include "UI/DeveloperConvenienceWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "UI/PreSetLoadWidget.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeveloperConvenienceMenuTest,
	"Outlier.UI.DeveloperConvenience.MenuSelectionAndFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeveloperConvenienceMenuTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Menu world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	auto* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	ON_SCOPE_EXIT
	{
		Controller->Player = nullptr;
		Player->PlayerController = nullptr;
	};
	auto* Menu = CreateWidget<UDeveloperConvenienceWidget>(Controller);
	if (!TestNotNull(TEXT("Menu"), Menu))
	{
		return false;
	}
	Menu->SetPlayerContext(FLocalPlayerContext(Player, World));
	Menu->LevelSelectionPanel = NewObject<UVerticalBox>(Menu);
	Menu->SelectedStageText = NewObject<UTextBlock>(Menu);
	Menu->FeedbackText = NewObject<UTextBlock>(Menu);
	Menu->ConfirmMoveButton = NewObject<UButton>(Menu);
	Menu->TeamRecoveryButton = NewObject<UDeveloperConvenienceActionWidget>(Menu);
	Menu->LevelSelectionButton = NewObject<UDeveloperConvenienceActionWidget>(Menu);
	Menu->ReturnToTitleButton = NewObject<UDeveloperConvenienceActionWidget>(Menu);
	Menu->PresetLoad = CreateWidget<UPreSetLoadWidget>(Controller);
	Menu->PresetLoad->SetPlayerContext(FLocalPlayerContext(Player, World));
	TArray<UButton*> StageButtons;
	for (FName ButtonName : { FName(TEXT("Level01Button")), FName(TEXT("Level02Button")), FName(TEXT("Level03Button")) })
	{
		auto* StageButton = NewObject<UButton>(Menu->PresetLoad);
		FButtonStyle Style = StageButton->GetStyle();
		Style.Normal.TintColor = FSlateColor(FLinearColor::Red);
		Style.Hovered.TintColor = FSlateColor(FLinearColor::Green);
		StageButton->SetStyle(Style);
		FindFProperty<FObjectPropertyBase>(UPreSetLoadWidget::StaticClass(), ButtonName)
			->SetObjectPropertyValue_InContainer(Menu->PresetLoad, StageButton);
		StageButtons.Add(StageButton);
	}
	Menu->PresetLoad->NativeConstruct();
	Menu->NativeConstruct();
	ON_SCOPE_EXIT
	{
		Menu->ReleaseDeveloperConvenienceContext();
		Menu->NativeDestruct();
	};
	FDeveloperConvenienceSnapshot Context;
	Context.MatchId = FGuid::NewGuid();
	Context.SessionId = FGuid::NewGuid();
	Context.bIsOperator = true;
	Menu->InitializeDeveloperConvenience(Context);
	TestEqual(TEXT("Initial list is collapsed"), Menu->LevelSelectionPanel->GetVisibility(), ESlateVisibility::Collapsed);
	Menu->LevelSelectionButton->OnActionClicked.Broadcast(EDeveloperConvenienceAction::LevelSelection);
	TestEqual(TEXT("Level action expands the list"), Menu->GetDeveloperConveniencePage(), EDeveloperConveniencePage::LevelSelection);
	TestFalse(TEXT("Move requires a selected stage"), Menu->ConfirmMoveButton->GetIsEnabled());
	StageButtons[1]->OnClicked.Broadcast();
	TestEqual(TEXT("Stage click only saves the choice"), Menu->GetPendingStageId(), OutlierPresetStageIds::Level2);
	TestFalse(TEXT("Stage click never sends a request"), Menu->bActionPending);
	TestTrue(TEXT("Selected stage enables confirmation"), Menu->ConfirmMoveButton->GetIsEnabled());
	StageButtons[2]->OnHovered.Broadcast();
	TestEqual(TEXT("Hover preserves the clicked destination"), Menu->GetPendingStageId(), OutlierPresetStageIds::Level2);
	TestEqual(TEXT("Clicked stage stays highlighted while another stage is hovered"),
		StageButtons[1]->GetStyle().Normal.TintColor.GetSpecifiedColor(), FLinearColor::Green);
	Menu->PresetLoad->SetRetainSelectedStage(false);
	TestEqual(TEXT("Default GameOver presentation still follows hover only"),
		StageButtons[1]->GetStyle().Normal.TintColor.GetSpecifiedColor(), FLinearColor::Red);
	Menu->PresetLoad->SetRetainSelectedStage(true);
	TestTrue(TEXT("Description identifies the chosen destination"), Menu->SelectedStageText->GetText().ToString().Contains(TEXT("다크팩토리")));
	// 서버 GM이 없는 격리 월드의 실제 RPC 거절을 받아 동기 응답과 잠금 해제를 검증한다.
	Menu->ConfirmMoveButton->OnClicked.Broadcast();
	TestFalse(TEXT("Synchronous rejection does not leave pending input"), Menu->bActionPending);
	TestTrue(TEXT("Rejected confirmation allows retry"), Menu->ConfirmMoveButton->GetIsEnabled());
	TestFalse(TEXT("Rejected request has feedback"), Menu->FeedbackText->GetText().IsEmpty());
	TestEqual(TEXT("Rejected move preserves selected stage"), Menu->GetPendingStageId(), OutlierPresetStageIds::Level2);
	Menu->HandleUILayerEscape_Implementation();
	TestEqual(TEXT("Escape closes the sub-list only"), Menu->GetDeveloperConveniencePage(), EDeveloperConveniencePage::MainMenu);
	TestEqual(TEXT("Escape preserves the pause session"), Menu->Context.SessionId, Context.SessionId);
	TestTrue(TEXT("Closing clears the selected destination"), Menu->GetPendingStageId().IsNone());
	Menu->TeamRecoveryButton->OnActionClicked.Broadcast(EDeveloperConvenienceAction::TeamRecovery);
	TestFalse(TEXT("Synchronous recovery rejection unlocks the menu"), Menu->bActionPending);
	FDeveloperConvenienceOptionResult OptionResult;
	OptionResult.RequestId = FGuid::NewGuid();
	OptionResult.MatchId = Context.MatchId;
	OptionResult.SessionId = Context.SessionId;
	OptionResult.Result = EDeveloperConvenienceRequestResult::Accepted;
	OptionResult.bRequestedValue = true;
	Controller->OnDeveloperConvenienceOptionResult.Broadcast(OptionResult);
	TestTrue(TEXT("Accepted option has success feedback"), Menu->FeedbackText->GetText().ToString().Contains(TEXT("ON")));
	const FString LastMessage = Menu->FeedbackText->GetText().ToString();
	OptionResult.SessionId = FGuid::NewGuid();
	OptionResult.Result = EDeveloperConvenienceRequestResult::InvalidPair;
	Controller->OnDeveloperConvenienceOptionResult.Broadcast(OptionResult);
	TestEqual(TEXT("Old session feedback is ignored"), Menu->FeedbackText->GetText().ToString(), LastMessage);

	FDeveloperConvenienceTitleResult TitleResult;
	TitleResult.RequestId = FGuid::NewGuid();
	TitleResult.MatchId = Context.MatchId;
	TitleResult.SessionId = Context.SessionId;
	TitleResult.GameplayGeneration = Context.GameplayGeneration;
	TitleResult.Result = EDeveloperConvenienceRequestResult::Accepted;
	Menu->bActionPending = true;
	Menu->PendingAction = EDeveloperConvenienceAction::ReturnToTitle;
	Menu->PendingRequestId = FGuid::NewGuid();
	Controller->OnDeveloperConvenienceTitleResult.Broadcast(TitleResult);
	TestTrue(TEXT("Another request cannot finish pending action"), Menu->bActionPending);
	TitleResult.RequestId = Menu->PendingRequestId;
	Controller->OnDeveloperConvenienceTitleResult.Broadcast(TitleResult);
	TestTrue(TEXT("Accepted exit locks before state replication"), Menu->bExitAccepted);
	TestFalse(TEXT("Accepted exit disables other actions"), Menu->TeamRecoveryButton->GetIsEnabled());
	Menu->HandleUILayerEscape_Implementation();
	TestTrue(TEXT("Escape cannot reopen an accepted exit"), Menu->bExitAccepted);
	Context.SessionId = FGuid::NewGuid();
	Menu->InitializeDeveloperConvenience(Context);
	TestTrue(TEXT("A new menu clears old feedback"), Menu->FeedbackText->GetText().IsEmpty());
	TestFalse(TEXT("A new menu clears the terminal lock"), Menu->bExitAccepted);
	// 실제 재생 경로에서 서버 종료 요청이 애니메이션 완료 전에는 나가지 않는지 확인한다.
	Menu->MenuSlide = NewObject<UWidgetAnimation>(Menu);
	Menu->MenuSlide->MovieScene = NewObject<UMovieScene>(Menu->MenuSlide);
	Menu->MenuSlide->MovieScene->SetTickResolutionDirectly(FFrameRate(30, 1));
	Menu->MenuSlide->MovieScene->SetPlaybackRange(FFrameNumber(0), 9);
	Menu->NativeConstruct();
	Menu->FlushAnimations();
	TestTrue(TEXT("Construct starts the opening animation forward"), Menu->IsAnimationPlayingForward(Menu->MenuSlide));
	Menu->SetAnimationCurrentTime(Menu->MenuSlide, 0.15f);
	Menu->RequestAnimatedClose();
	TestFalse(TEXT("Close reverses an opening animation"), Menu->IsAnimationPlayingForward(Menu->MenuSlide));
	TestTrue(TEXT("Animated close stays pending until completion"), Menu->bClosing);
	TestFalse(TEXT("Animation start does not request server resume"), Menu->bCloseRequestSent);
	TestFalse(TEXT("Closing disables the entire menu including child toggles"), Menu->GetIsEnabled());
	Menu->RequestAnimatedClose();
	Menu->ExecuteDeveloperConvenienceAction(EDeveloperConvenienceAction::TeamRecovery);
	Menu->HandleUILayerEscape_Implementation();
	TestFalse(TEXT("Repeated close and actions cannot bypass the animation"), Menu->bCloseRequestSent);
	Menu->TickActionsAndAnimation(0.5f);
	Menu->FlushAnimations();
	Menu->TickActionsAndAnimation(0.5f);
	Menu->FlushAnimations();
	TestFalse(TEXT("Synchronous close rejection unlocks the menu"), Menu->bClosing);
	TestTrue(TEXT("Rejected close restores child input"), Menu->GetIsEnabled());
	TestFalse(TEXT("Rejected close displays feedback"), Menu->FeedbackText->GetText().IsEmpty());
	Menu->StopAnimation(Menu->MenuSlide);
	Menu->MenuSlide = nullptr;
	Menu->RequestAnimatedClose();
	TestFalse(TEXT("Missing animation uses immediate close and handles rejection"), Menu->bClosing);
	// 강제 정리 후 도착한 이전 완료 알림은 다음 세션의 종료를 요청할 수 없다.
	Menu->bClosing = true;
	Menu->ReleaseDeveloperConvenienceContext();
	Menu->HandleMenuSlideFinished();
	TestFalse(TEXT("Released animation cannot submit a close request"), Menu->bCloseRequestSent);
	Menu->InitializeDeveloperConvenience(Context);
	Context.bIsOperator = false;
	Menu->InitializeDeveloperConvenience(Context);
	TestFalse(TEXT("Waiting player cannot execute actions"), Menu->CanOperate());
	Menu->ReleaseDeveloperConvenienceContext();
	Menu->NativeDestruct();
	Controller->OnDeveloperConvenienceTitleResult.Broadcast(TitleResult);
	TestTrue(TEXT("Removed menu does not receive old feedback"), Menu->FeedbackText->GetText().IsEmpty());
	Menu->NativeConstruct();
	return true;
}

#endif
