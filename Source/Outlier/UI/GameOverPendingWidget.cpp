#include "UI/GameOverPendingWidget.h"

#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "FirstPerson/FirstPersonPlayerController.h"

UGameOverPendingWidget::UGameOverPendingWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	ReceiverChoiceTexts.Add(EGameOverPendingChoice::Continue,
		NSLOCTEXT("GameOverPending", "ReceiverContinue", "상대가 이어하기를 제안했습니다."));
	ReceiverChoiceTexts.Add(EGameOverPendingChoice::PresetLevel,
		NSLOCTEXT("GameOverPending", "ReceiverPreset", "상대가 스테이지를 선택했습니다."));
	ReceiverChoiceTexts.Add(EGameOverPendingChoice::MainMenu,
		NSLOCTEXT("GameOverPending", "ReceiverMainMenu", "상대가 메인 화면으로 돌아가기를 제안했습니다."));
	ReceiverLevelTexts.Add(1, NSLOCTEXT("GameOverPending", "ReceiverLevel1", "스테이지 1"));
	ReceiverLevelTexts.Add(2, NSLOCTEXT("GameOverPending", "ReceiverLevel2", "스테이지 2"));
	ReceiverLevelTexts.Add(3, NSLOCTEXT("GameOverPending", "ReceiverLevel3", "스테이지 3"));
	ReceiverLevelTexts.Add(4, NSLOCTEXT("GameOverPending", "ReceiverLevel4", "스테이지 4"));
}

void UGameOverPendingWidget::InitializePendingRequest(
	const FGameOverPendingRequest& InRequest,
	bool bInRequester)
{
	PendingRequest = InRequest;
	bIsRequester = bInRequester;
	bResponseSubmitted = false;
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] Initialize Widget=%s Role=%s Choice=%d Level=%d"),
		*GetNameSafe(this), bIsRequester ? TEXT("Sender") : TEXT("Receiver"),
		static_cast<int32>(PendingRequest.Choice), PendingRequest.LevelIndex);
	RefreshView();
}

void UGameOverPendingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshView();
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] Construct Widget=%s Focusable=%d Switcher=%s ChoiceText=%s LevelText=%s"),
		*GetNameSafe(this), IsFocusable() ? 1 : 0,
		*GetNameSafe(PendingSwitcher), *GetNameSafe(ReceiverChoiceText),
		*GetNameSafe(ReceiverLevelText));
}

bool UGameOverPendingWidget::HandleUILayerConfirmed_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[GameOverPending][Input] Confirmed Widget=%s"), *GetNameSafe(this));
	return SubmitResponse(true);
}

bool UGameOverPendingWidget::HandleUILayerEscape_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[GameOverPending][Input] Escape Widget=%s"), *GetNameSafe(this));
	return SubmitResponse(false);
}

bool UGameOverPendingWidget::SubmitResponse(bool bApprove)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] Submit Widget=%s Approve=%d Requester=%d AlreadySubmitted=%d Owner=%s"),
		*GetNameSafe(this), bApprove ? 1 : 0, bIsRequester ? 1 : 0,
		bResponseSubmitted ? 1 : 0, *GetNameSafe(GetOwningPlayer()));
	// 요청자 쪽 입력은 위젯을 닫거나 서버 판정을 바꾸지 않는다.
	if (bIsRequester || bResponseSubmitted)
	{
		return true;
	}

	if (AFirstPersonPlayerController* Controller =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		bResponseSubmitted = true;
		Controller->RequestGameOverPendingResponse(PendingRequest, bApprove);
	}
	return true;
}

void UGameOverPendingWidget::RefreshView()
{
	if (PendingSwitcher)
	{
		PendingSwitcher->SetActiveWidgetIndex(bIsRequester ? 0 : 1);
	}
	if (ReceiverChoiceText)
	{
		const FText* ChoiceText = ReceiverChoiceTexts.Find(PendingRequest.Choice);
		ReceiverChoiceText->SetText(ChoiceText ? *ChoiceText : FText::GetEmpty());
	}
	if (ReceiverLevelText)
	{
		const bool bShowLevel = PendingRequest.Choice == EGameOverPendingChoice::PresetLevel
			&& PendingRequest.LevelIndex >= 1 && PendingRequest.LevelIndex <= 4;
		const FText* LevelText = bShowLevel
			? ReceiverLevelTexts.Find(PendingRequest.LevelIndex)
			: nullptr;
		ReceiverLevelText->SetText(LevelText ? *LevelText : FText::GetEmpty());
		ReceiverLevelText->SetVisibility(bShowLevel
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}
}
