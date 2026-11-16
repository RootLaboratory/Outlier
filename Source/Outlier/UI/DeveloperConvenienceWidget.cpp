#include "UI/DeveloperConvenienceWidget.h"

#include "FirstPerson/FirstPersonPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/PreSetLoadWidget.h"
#include "Animation/WidgetAnimation.h"

#define LOCTEXT_NAMESPACE "DeveloperConvenienceWidget"

void UDeveloperConvenienceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	for (auto* ActionWidget : { TeamRecoveryButton.Get(), LevelSelectionButton.Get(), ReturnToTitleButton.Get() })
	{
		if (ActionWidget)
		{
			ActionWidget->OnActionClicked.AddUniqueDynamic(this, &ThisClass::ExecuteDeveloperConvenienceAction);
		}
	}
	if (PresetLoad)
	{
		PresetLoad->OnPresetStageConfirmed.AddUniqueDynamic(this, &ThisClass::HandleStageSelected);
		// GameOver의 즉시 선택은 유지하고 이 메뉴에서만 클릭한 레벨 표시를 보존한다.
		PresetLoad->SetRetainSelectedStage(true);
	}
	if (ConfirmMoveButton)
	{
		ConfirmMoveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmSelectedStage);
	}
	ObservedController = Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	if (auto* Controller = ObservedController.Get())
	{
		Controller->OnDeveloperConvenienceOptionResult.AddUniqueDynamic(this, &ThisClass::HandleOptionResult);
		Controller->OnDeveloperConvenienceRecoveryResult.AddUniqueDynamic(this, &ThisClass::HandleRecoveryResult);
		Controller->OnDeveloperConveniencePresetResult.AddUniqueDynamic(this, &ThisClass::HandlePresetResult);
		Controller->OnDeveloperConvenienceTitleResult.AddUniqueDynamic(this, &ThisClass::HandleTitleResult);
		Controller->OnDeveloperConvenienceSessionResult.AddUniqueDynamic(this, &ThisClass::HandleSessionResult);
	}
	RefreshMenu();
	if (MenuSlide)
	{
		FWidgetAnimationDynamicEvent Finished;
		Finished.BindDynamic(this, &ThisClass::HandleMenuSlideFinished);
		BindToAnimationFinished(MenuSlide, Finished);
		if (bContextActive && Context.bIsOperator)
		{
			PlayAnimationForward(MenuSlide);
		}
	}
}

void UDeveloperConvenienceWidget::ReleaseDeveloperConvenienceContext()
{
	// 강제 퇴장이나 세션 교체는 연출을 기다리지 않는다. 이전 완료 알림의 종료 요청을 차단한다.
	bContextActive = false;
	bClosing = false;
	bCloseRequestSent = false;
	CloseRequestId.Invalidate();
}

void UDeveloperConvenienceWidget::RequestAnimatedClose()
{
	if (!CanOperate())
	{
		return;
	}
	bClosing = true;
	bCloseRequestSent = false;
	CloseRequestId.Invalidate();
	RefreshMenu();
	// 월드 정지 중에도 동작하는 UMG 재생 완료를 사용하고 게임 시간 타이머는 추가하지 않는다.
	if (MenuSlide && MenuSlide->GetEndTime() > MenuSlide->GetStartTime()
		&& PlayAnimationReverse(MenuSlide).IsValid())
	{
		return;
	}
	SendCloseRequest();
}

void UDeveloperConvenienceWidget::HandleMenuSlideFinished()
{
	if (bClosing)
	{
		SendCloseRequest();
	}
}

void UDeveloperConvenienceWidget::SendCloseRequest()
{
	auto* Controller = ObservedController.Get();
	if (!bContextActive || !bClosing || bCloseRequestSent || !Controller || !Context.bIsOperator
		|| Context.bTransitioning || bExitAccepted)
	{
		return;
	}
	bCloseRequestSent = true;
	const FGuid RequestId = Controller->RequestDeveloperConvenienceSession(false, Context);
	// Listen/Standalone의 동기 응답으로 이미 해제·제거됐다면 처리된 ID를 다시 저장하지 않는다.
	if (bContextActive && bClosing && bCloseRequestSent)
	{
		CloseRequestId = RequestId;
	}
}

void UDeveloperConvenienceWidget::HandleSessionResult(const FDeveloperConvenienceSessionResult& Result)
{
	if (!bClosing || !bCloseRequestSent || Result.bOpen || !IsCurrentResult(Result.MatchId, Result.SessionId)
		|| (CloseRequestId.IsValid() && CloseRequestId != Result.RequestId))
	{
		return;
	}
	if (Result.Result == EDeveloperConvenienceRequestResult::Accepted)
	{
		// 서버 상태가 도착해 레이어가 제거될 때까지 입력을 복구하지 않는다.
		return;
	}
	bClosing = false;
	bCloseRequestSent = false;
	CloseRequestId.Invalidate();
	SetFeedback(GetFailureText(Result.Result));
	RefreshMenu();
	if (MenuSlide)
	{
		PlayAnimationForward(MenuSlide);
	}
}

void UDeveloperConvenienceWidget::InitializeDeveloperConvenience(const FDeveloperConvenienceSnapshot& Snapshot)
{
	const bool bNewSession = !bContextActive || Context.SessionId != Snapshot.SessionId;
	Context = Snapshot;
	bContextActive = true;
	SetIsFocusable(true);
	if (bNewSession)
	{
		bClosing = false;
		bCloseRequestSent = false;
		CloseRequestId.Invalidate();
		Page = EDeveloperConveniencePage::MainMenu;
		PendingStageId = NAME_None;
		PendingRequestId.Invalidate();
		bActionPending = false;
		bExitAccepted = false;
		if (PresetLoad)
		{
			PresetLoad->ResetStageSelection();
		}
		SetFeedback(FText::GetEmpty());
		OnDeveloperConveniencePageChanged.Broadcast(Page);
	}
	RefreshMenu();
}

bool UDeveloperConvenienceWidget::SetDeveloperConveniencePage(EDeveloperConveniencePage NewPage)
{
	if (!CanOperate()
		|| (NewPage != EDeveloperConveniencePage::MainMenu && NewPage != EDeveloperConveniencePage::LevelSelection))
	{
		return false;
	}
	if (Page != NewPage)
	{
		Page = NewPage;
		PendingStageId = NAME_None;
		if (PresetLoad)
		{
			PresetLoad->ResetStageSelection();
		}
		RefreshMenu();
		OnDeveloperConveniencePageChanged.Broadcast(Page);
	}
	return true;
}

FReply UDeveloperConvenienceWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (auto* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		if (Controller->HandleDeveloperConvenienceKey(Event.GetKey(), Event.IsShiftDown(), Event.IsRepeat()))
		{
			return FReply::Handled();
		}
		if (auto* Layers = Controller->GetLocalPlayer()
			? Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
			Layers && Layers->RouteWidgetPreviewKeyDown(this, Event))
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

bool UDeveloperConvenienceWidget::HandleUILayerEscape_Implementation()
{
	if (bClosing || bActionPending || bExitAccepted || Context.bTransitioning)
	{
		return true;
	}
	if (bContextActive && Context.bIsOperator)
	{
		if (Page == EDeveloperConveniencePage::LevelSelection)
		{
			SetDeveloperConveniencePage(EDeveloperConveniencePage::MainMenu);
			return true;
		}
		RequestAnimatedClose();
	}
	// 대기 화면도 입력을 소비해 기존 설정창/기본 Pop 동작으로 빠지지 않는다.
	return true;
}

void UDeveloperConvenienceWidget::NativeDestruct()
{
	bClosing = false;
	bCloseRequestSent = false;
	CloseRequestId.Invalidate();
	if (MenuSlide)
	{
		FWidgetAnimationDynamicEvent Finished;
		Finished.BindDynamic(this, &ThisClass::HandleMenuSlideFinished);
		UnbindFromAnimationFinished(MenuSlide, Finished);
	}
	for (auto* ActionWidget : { TeamRecoveryButton.Get(), LevelSelectionButton.Get(), ReturnToTitleButton.Get() })
	{
		if (ActionWidget)
		{
			ActionWidget->OnActionClicked.RemoveDynamic(this, &ThisClass::ExecuteDeveloperConvenienceAction);
		}
	}
	if (PresetLoad)
	{
		PresetLoad->OnPresetStageConfirmed.RemoveDynamic(this, &ThisClass::HandleStageSelected);
		PresetLoad->SetRetainSelectedStage(false);
	}
	if (ConfirmMoveButton)
	{
		ConfirmMoveButton->OnClicked.RemoveDynamic(this, &ThisClass::ConfirmSelectedStage);
	}
	if (auto* Controller = ObservedController.Get())
	{
		Controller->OnDeveloperConvenienceOptionResult.RemoveDynamic(this, &ThisClass::HandleOptionResult);
		Controller->OnDeveloperConvenienceRecoveryResult.RemoveDynamic(this, &ThisClass::HandleRecoveryResult);
		Controller->OnDeveloperConveniencePresetResult.RemoveDynamic(this, &ThisClass::HandlePresetResult);
		Controller->OnDeveloperConvenienceTitleResult.RemoveDynamic(this, &ThisClass::HandleTitleResult);
		Controller->OnDeveloperConvenienceSessionResult.RemoveDynamic(this, &ThisClass::HandleSessionResult);
	}
	ObservedController.Reset();
	if (bContextActive)
	{
		bContextActive = false;
		if (auto* Controller = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
		{
			Controller->ServerDeveloperConvenienceUIUnavailable(Context.SessionId);
		}
	}
	Super::NativeDestruct();
}

bool UDeveloperConvenienceWidget::CanOperate() const
{
	return bContextActive && Context.bIsOperator && Context.MatchId.IsValid() && Context.SessionId.IsValid()
		&& !Context.bTransitioning && !bActionPending && !bExitAccepted && !bClosing;
}

void UDeveloperConvenienceWidget::RefreshMenu()
{
	// 하위 토글까지 함께 잠가 닫는 도중 클릭으로 추가 요청이 발생하지 않도록 한다.
	SetIsEnabled(!bClosing);
	const bool bEnabled = CanOperate();
	for (auto* ActionWidget : { TeamRecoveryButton.Get(), LevelSelectionButton.Get(), ReturnToTitleButton.Get() })
	{
		if (ActionWidget)
		{
			ActionWidget->SetIsEnabled(bEnabled);
		}
	}
	if (LevelSelectionPanel)
	{
		LevelSelectionPanel->SetVisibility(Page == EDeveloperConveniencePage::LevelSelection
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PresetLoad)
	{
		PresetLoad->SetIsEnabled(bEnabled && Page == EDeveloperConveniencePage::LevelSelection);
	}
	if (ConfirmMoveButton)
	{
		ConfirmMoveButton->SetIsEnabled(bEnabled && Page == EDeveloperConveniencePage::LevelSelection && !PendingStageId.IsNone());
	}
	if (SelectedStageText)
	{
		SelectedStageText->SetText(PendingStageId.IsNone() ? LOCTEXT("ChooseStage", "이동할 레벨을 선택해 주세요.")
			: FText::Format(LOCTEXT("StageDescription", "{0}로 함께 이동합니다.\n전투와 업그레이드가 초기화됩니다."), GetStageName(PendingStageId)));
	}
}

void UDeveloperConvenienceWidget::ExecuteDeveloperConvenienceAction(EDeveloperConvenienceAction Action)
{
	if (!CanOperate())
	{
		return;
	}
	if (Action == EDeveloperConvenienceAction::LevelSelection)
	{
		SetDeveloperConveniencePage(Page == EDeveloperConveniencePage::MainMenu
			? EDeveloperConveniencePage::LevelSelection : EDeveloperConveniencePage::MainMenu);
		return;
	}
	auto* Controller = ObservedController.Get();
	if (!Controller || (Action != EDeveloperConvenienceAction::TeamRecovery && Action != EDeveloperConvenienceAction::ReturnToTitle))
	{
		return;
	}
	bActionPending = true;
	PendingAction = Action;
	PendingRequestId.Invalidate();
	RefreshMenu();
	const FGuid RequestId = Action == EDeveloperConvenienceAction::TeamRecovery
		? Controller->RequestDeveloperConvenienceTeamRecovery(Context)
		: Controller->RequestDeveloperConvenienceReturnToTitle(Context);
	// Listen/Standalone 결과는 호출이 반환되기 전에 도착할 수 있다. 처리된 요청을 다시 대기로 만들지 않는다.
	if (bActionPending)
	{
		PendingRequestId = RequestId;
	}
}

void UDeveloperConvenienceWidget::HandleStageSelected(FName StageId)
{
	if (!CanOperate() || Page != EDeveloperConveniencePage::LevelSelection)
	{
		return;
	}
	if (!OutlierPresetStageIds::All().Contains(StageId))
	{
		PendingStageId = NAME_None;
		SetFeedback(GetFailureText(EDeveloperConvenienceRequestResult::InvalidStage));
	}
	else
	{
		// 기존 모듈의 클릭 이벤트는 선택만 보관한다. 서버 이동은 이동 확인 버튼에서 한 번 요청한다.
		PendingStageId = StageId;
	}
	RefreshMenu();
}

void UDeveloperConvenienceWidget::ConfirmSelectedStage()
{
	auto* Controller = ObservedController.Get();
	if (!CanOperate() || !Controller || Page != EDeveloperConveniencePage::LevelSelection || PendingStageId.IsNone())
	{
		return;
	}
	bActionPending = true;
	PendingAction = EDeveloperConvenienceAction::LevelSelection;
	PendingRequestId.Invalidate();
	RefreshMenu();
	const FGuid RequestId = Controller->RequestDeveloperConveniencePreset(PendingStageId, Context);
	if (bActionPending)
	{
		PendingRequestId = RequestId;
	}
}

FText UDeveloperConvenienceWidget::GetStageName(FName StageId) const
{
	if (const FText* DisplayName = StageDisplayNames.Find(StageId))
	{
		return *DisplayName;
	}
	if (StageId == OutlierPresetStageIds::Level1)
	{
		return LOCTEXT("Level1", "서버실");
	}
	if (StageId == OutlierPresetStageIds::Level2)
	{
		return LOCTEXT("Level2", "다크팩토리");
	}
	if (StageId == OutlierPresetStageIds::Level3)
	{
		return LOCTEXT("Level3", "엘리베이터");
	}
	return FText::FromName(StageId);
}

void UDeveloperConvenienceWidget::SetFeedback(const FText& Message)
{
	if (FeedbackText)
	{
		FeedbackText->SetText(Message);
	}
}

bool UDeveloperConvenienceWidget::IsCurrentResult(const FGuid& MatchId, const FGuid& SessionId) const
{
	return bContextActive && Context.bIsOperator && Context.MatchId == MatchId && Context.SessionId == SessionId
		&& MatchId.IsValid() && SessionId.IsValid();
}

bool UDeveloperConvenienceWidget::FinishActionResult(const FGuid& RequestId, const FGuid& MatchId, const FGuid& SessionId,
	int64 Generation, EDeveloperConvenienceAction Action)
{
	if (!IsCurrentResult(MatchId, SessionId) || Context.GameplayGeneration != Generation || !bActionPending
		|| PendingAction != Action || !RequestId.IsValid() || (PendingRequestId.IsValid() && PendingRequestId != RequestId))
	{
		return false;
	}
	bActionPending = false;
	PendingRequestId.Invalidate();
	return true;
}

FText UDeveloperConvenienceWidget::GetFailureText(EDeveloperConvenienceRequestResult Result) const
{
	switch (Result)
	{
	case EDeveloperConvenienceRequestResult::InvalidPair:
		return LOCTEXT("InvalidPair", "플레이어 연결 상태를 확인해 주세요.");
	case EDeveloperConvenienceRequestResult::StaleContext:
	case EDeveloperConvenienceRequestResult::NoSession:
		return LOCTEXT("Stale", "메뉴 상태가 변경되었습니다. 메뉴를 다시 열어 주세요.");
	case EDeveloperConvenienceRequestResult::NotOperator:
		return LOCTEXT("NotOperator", "메뉴를 연 플레이어만 조작할 수 있습니다.");
	case EDeveloperConvenienceRequestResult::Transitioning:
		return LOCTEXT("Transitioning", "이동 또는 종료 처리 중입니다.");
	case EDeveloperConvenienceRequestResult::UIUnavailable:
		return LOCTEXT("UIUnavailable", "필요한 UI가 준비되지 않아 실행할 수 없습니다.");
	case EDeveloperConvenienceRequestResult::TargetUnavailable:
		return LOCTEXT("TargetUnavailable", "대상이 준비되지 않아 실행할 수 없습니다.");
	case EDeveloperConvenienceRequestResult::InvalidStage:
		return LOCTEXT("InvalidStage", "이 레벨로는 이동할 수 없습니다.");
	default:
		return LOCTEXT("Failed", "현재는 실행할 수 없습니다.");
	}
}

void UDeveloperConvenienceWidget::HandleOptionResult(const FDeveloperConvenienceOptionResult& Result)
{
	if (!IsCurrentResult(Result.MatchId, Result.SessionId) || bExitAccepted)
	{
		return;
	}
	if (Result.Result != EDeveloperConvenienceRequestResult::Accepted)
	{
		SetFeedback(GetFailureText(Result.Result));
		return;
	}
	switch (Result.Option)
	{
	case EDeveloperConvenienceOption::Invincible:
		SetFeedback(Result.bRequestedValue ? LOCTEXT("InvincibleOn", "두 플레이어 무적 ON") : LOCTEXT("InvincibleOff", "두 플레이어 무적 OFF"));
		break;
	case EDeveloperConvenienceOption::ConsumeCurrency:
		SetFeedback(Result.bRequestedValue ? LOCTEXT("CurrencyOn", "재화 소비 ON — 구매 비용이 차감됩니다.")
			: LOCTEXT("CurrencyOff", "재화 소비 OFF — 구매 비용이 면제됩니다."));
		break;
	case EDeveloperConvenienceOption::FastCooldown:
		SetFeedback(Result.bRequestedValue ? LOCTEXT("CooldownOn", "빠른 재사용 ON — 스킬 쿨타임을 1초로 적용합니다.")
			: LOCTEXT("CooldownOff", "빠른 재사용 OFF — 미완료 쿨타임을 원래 기준으로 복원합니다."));
		break;
	}
}

void UDeveloperConvenienceWidget::HandleRecoveryResult(const FDeveloperConvenienceRecoveryResult& Result)
{
	if (FinishActionResult(Result.RequestId, Result.MatchId, Result.SessionId, Result.GameplayGeneration,
		EDeveloperConvenienceAction::TeamRecovery))
	{
		SetFeedback(Result.Result == EDeveloperConvenienceRequestResult::Accepted
			? LOCTEXT("Recovered", "팀 체력과 Shooter 기본 실드를 회복했습니다. Partner Reboot를 완료하고 추가 실드는 유지합니다.")
			: GetFailureText(Result.Result));
		RefreshMenu();
	}
}

void UDeveloperConvenienceWidget::HandlePresetResult(const FDeveloperConveniencePresetResult& Result)
{
	if (FinishActionResult(Result.RequestId, Result.MatchId, Result.SessionId, Result.GameplayGeneration,
		EDeveloperConvenienceAction::LevelSelection))
	{
		bExitAccepted = Result.Result == EDeveloperConvenienceRequestResult::Accepted;
		SetFeedback(bExitAccepted ? FText::Format(LOCTEXT("Moving", "{0}로 이동합니다."), GetStageName(Result.StageId))
			: GetFailureText(Result.Result));
		RefreshMenu();
	}
}

void UDeveloperConvenienceWidget::HandleTitleResult(const FDeveloperConvenienceTitleResult& Result)
{
	if (FinishActionResult(Result.RequestId, Result.MatchId, Result.SessionId, Result.GameplayGeneration,
		EDeveloperConvenienceAction::ReturnToTitle))
	{
		bExitAccepted = Result.Result == EDeveloperConvenienceRequestResult::Accepted;
		SetFeedback(bExitAccepted ? LOCTEXT("Returning", "두 플레이어가 타이틀로 돌아갑니다.") : GetFailureText(Result.Result));
		RefreshMenu();
	}
}

bool UDeveloperConvenienceWidget::HandleUILayerConfirmed_Implementation()
{
	if (CanOperate() && Page == EDeveloperConveniencePage::LevelSelection && PresetLoad)
	{
		if (PendingStageId.IsNone())
		{
			PresetLoad->ConfirmStageSelection();
		}
		else
		{
			ConfirmSelectedStage();
		}
	}
	return true;
}

bool UDeveloperConvenienceWidget::HandleUILayerUp_Implementation()
{
	if (CanOperate() && Page == EDeveloperConveniencePage::LevelSelection && PresetLoad)
	{
		PendingStageId = NAME_None;
		PresetLoad->MoveStageSelection(-1);
		RefreshMenu();
	}
	return true;
}

bool UDeveloperConvenienceWidget::HandleUILayerDown_Implementation()
{
	if (CanOperate() && Page == EDeveloperConveniencePage::LevelSelection && PresetLoad)
	{
		PendingStageId = NAME_None;
		PresetLoad->MoveStageSelection(1);
		RefreshMenu();
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
