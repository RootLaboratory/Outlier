#include "UI/GameOverWidget.h"
#include "UI/GameOverPendingWidget.h"
#include "UI/GameOverChoiceWidget.h"
#include "UI/UILayerKeyHintWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "UObject/ConstructorHelpers.h"

UGameOverWidget::UGameOverWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	static ConstructorHelpers::FClassFinder<UUILayerKeyHintWidget> HintWidget(
		TEXT("/Game/Blueprints/Widget/Outlier/WBP_UIHintKey"));
	KeyHintWidgetClass = HintWidget.Class;
}

TSubclassOf<UGameOverPendingWidget> UGameOverWidget::GetGameOverPendingWidgetClass() const
{
	return GameOverPendingWidgetClass;
}

TSubclassOf<UUILayerKeyHintWidget> UGameOverWidget::GetKeyHintWidgetClass() const
{
	return KeyHintWidgetClass;
}

void UGameOverWidget::SetImmediateSelectionMode(bool bImmediate)
{
	bImmediateSelections = bImmediate;
	if (ChoiceMenu)
	{
		ChoiceMenu->SetQuitGameAvailable(bImmediate);
	}
	if (QuitGameButton)
	{
		QuitGameButton->SetVisibility(bImmediate ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

bool UGameOverWidget::ShowPendingRequest(
	const FGameOverPendingRequest& Request,
	bool bIsRequester)
{
	if (bImmediateSelections)
	{
		return false;
	}
	UCanvasPanel* Canvas = GameOverCanvas
		? GameOverCanvas.Get()
		: Cast<UCanvasPanel>(GetRootWidget());
	if (!Canvas || !GameOverPendingWidgetClass || !GetOwningPlayer())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameOver] Pending widget needs a CanvasPanel and GameOverPendingWidgetClass on %s"),
			*GetNameSafe(GetClass()));
		return false;
	}

	ClosePendingRequest();
	UGameOverPendingWidget* Widget = CreateWidget<UGameOverPendingWidget>(
		GetOwningPlayer(), GameOverPendingWidgetClass);
	if (!Widget)
	{
		return false;
	}
	Widget->InitializePendingRequest(Request, bIsRequester);
	Widget->SetIsFocusable(true);
	Widget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	UCanvasPanelSlot* SwitcherSlot = Canvas->AddChildToCanvas(Widget);
	if (!SwitcherSlot)
	{
		return false;
	}
	SwitcherSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	SwitcherSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	SwitcherSlot->SetPosition(FVector2D::ZeroVector);
	SwitcherSlot->SetAutoSize(true);
	SwitcherSlot->SetZOrder(1000);
	ActivePendingWidget = Widget;
	Widget->SetUserFocus(GetOwningPlayer());
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] Show GameOver=%s Pending=%s Role=%s Choice=%d Level=%d Canvas=%s Focusable=%d"),
		*GetNameSafe(this), *GetNameSafe(Widget), bIsRequester ? TEXT("Sender") : TEXT("Receiver"),
		static_cast<int32>(Request.Choice), Request.LevelIndex,
		*GetNameSafe(Canvas), Widget->IsFocusable() ? 1 : 0);

	if (ChoiceMenu) ChoiceMenu->SetIsEnabled(false);
	if (ContinueButton) ContinueButton->SetIsEnabled(false);
	if (LevelSelectButton) LevelSelectButton->SetIsEnabled(false);
	if (MainMenuButton) MainMenuButton->SetIsEnabled(false);
	if (PresetLoad) PresetLoad->SetIsEnabled(false);
	HideStagePreview();
	return true;
}

void UGameOverWidget::ClosePendingRequest()
{
	if (!ActivePendingWidget)
	{
		return;
	}
	ActivePendingWidget->RemoveFromParent();
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] Close GameOver=%s"), *GetNameSafe(this));
	ActivePendingWidget = nullptr;
	if (ChoiceMenu) ChoiceMenu->SetIsEnabled(true);
	if (ContinueButton) ContinueButton->SetIsEnabled(true);
	if (LevelSelectButton) LevelSelectButton->SetIsEnabled(true);
	if (MainMenuButton) MainMenuButton->SetIsEnabled(true);
	if (PresetLoad) PresetLoad->SetIsEnabled(true);
}

bool UGameOverWidget::HandleUILayerConfirmed_Implementation()
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] GameOverConfirmed Pending=%s"), *GetNameSafe(ActivePendingWidget));
	if (ActivePendingWidget)
	{
		return IUILayerInputReceiver::Execute_HandleUILayerConfirmed(ActivePendingWidget);
	}
	if (PresetLoad && PresetLoad->IsVisible())
	{
		return PresetLoad->ConfirmStageSelection();
	}
	if (ChoiceMenu)
	{
		return IUILayerInputReceiver::Execute_HandleUILayerConfirmed(ChoiceMenu);
	}
	// Legacy WBP remains usable until its three buttons are moved into ChoiceMenu.
	if (QuitGameButton && QuitGameButton->IsVisible() && QuitGameButton->IsHovered()) HandleQuitGameButtonClicked();
	else if (MainMenuButton && MainMenuButton->IsHovered()) HandleMainMenuButtonClicked();
	else if (LevelSelectButton && LevelSelectButton->IsHovered()) HandleLevelSelectButtonClicked();
	else HandleContinueButtonClicked();
	return true;
}

bool UGameOverWidget::HandleUILayerEscape_Implementation()
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GameOverPending][Input] GameOverEscape Pending=%s"), *GetNameSafe(ActivePendingWidget));
	if (ActivePendingWidget)
	{
		return IUILayerInputReceiver::Execute_HandleUILayerEscape(ActivePendingWidget);
	}
	if (PresetLoad && PresetLoad->IsVisible())
	{
		ClosePresetLoad();
		return true;
	}
	return ChoiceMenu
		? IUILayerInputReceiver::Execute_HandleUILayerEscape(ChoiceMenu)
		: true;
}

bool UGameOverWidget::HandleUILayerUp_Implementation()
{
	if (ActivePendingWidget) return true;
	if (PresetLoad && PresetLoad->IsVisible()) { PresetLoad->MoveStageSelection(-1); return true; }
	return ChoiceMenu ? IUILayerInputReceiver::Execute_HandleUILayerUp(ChoiceMenu) : false;
}

bool UGameOverWidget::HandleUILayerDown_Implementation()
{
	if (ActivePendingWidget) return true;
	if (PresetLoad && PresetLoad->IsVisible()) { PresetLoad->MoveStageSelection(1); return true; }
	return ChoiceMenu ? IUILayerInputReceiver::Execute_HandleUILayerDown(ChoiceMenu) : false;
}

bool UGameOverWidget::HandleUILayerLeft_Implementation()
{
	if (ActivePendingWidget) return true;
	if (PresetLoad && PresetLoad->IsVisible()) { ClosePresetLoad(); return true; }
	return ChoiceMenu ? IUILayerInputReceiver::Execute_HandleUILayerLeft(ChoiceMenu) : false;
}

bool UGameOverWidget::HandleUILayerRight_Implementation()
{
	if (ActivePendingWidget) return true;
	if (PresetLoad && PresetLoad->IsVisible()) return true;
	return ChoiceMenu ? IUILayerInputReceiver::Execute_HandleUILayerRight(ChoiceMenu) : false;
}

void UGameOverWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// A nested WBP keeps its asset name unless it is renamed to ChoiceMenu in the designer.
	// Locate the module by class so its input and click delegates work in either case.
	if (!ChoiceMenu && WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (!ChoiceMenu)
			{
				ChoiceMenu = Cast<UGameOverChoiceWidget>(Widget);
			}
		});
	}
	if (ChoiceMenu)
	{
		ChoiceMenu->OnChoiceConfirmed.AddUniqueDynamic(this, &UGameOverWidget::HandleChoiceConfirmed);
		ChoiceMenu->OnEscapeRequested.AddUniqueDynamic(this, &UGameOverWidget::HandleChoiceEscape);
		UE_LOG(LogTemp, Log, TEXT("[GameOver] Bound choice widget %s"), *GetNameSafe(ChoiceMenu));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameOver] Choice widget was not found in %s; check WBP_GameOver child widget"),
			*GetNameSafe(this));
	}

	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddUniqueDynamic(
			this,
			&UGameOverWidget::HandleContinueButtonClicked);
	}

	if (LevelSelectButton)
	{
		LevelSelectButton->OnClicked.AddUniqueDynamic(
			this,
			&UGameOverWidget::HandleLevelSelectButtonClicked);
	}

	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.AddUniqueDynamic(
			this,
			&UGameOverWidget::HandleMainMenuButtonClicked);
	}

	if (QuitGameButton)
	{
		QuitGameButton->OnClicked.AddUniqueDynamic(this, &UGameOverWidget::HandleQuitGameButtonClicked);
	}

	if (PresetLoad)
	{
		PresetLoad->OnPresetStageConfirmed.AddUniqueDynamic(
			this,
			&UGameOverWidget::HandlePresetStageConfirmed);
		PresetLoad->OnPresetStageHoverChanged.AddUObject(
			this,
			&UGameOverWidget::HandlePresetStageHoverChanged);
	}
}

void UGameOverWidget::HandleChoiceConfirmed(EGameOverMenuChoice Choice)
{
	UE_LOG(LogTemp, Log, TEXT("[GameOver] Choice confirmed: %d"), static_cast<int32>(Choice));
	switch (Choice)
	{
	case EGameOverMenuChoice::Continue: HandleContinueButtonClicked(); break;
	case EGameOverMenuChoice::SelectLevel: HandleLevelSelectButtonClicked(); break;
	case EGameOverMenuChoice::MainMenu: HandleMainMenuButtonClicked(); break;
	case EGameOverMenuChoice::QuitGame: HandleQuitGameButtonClicked(); break;
	}
}

void UGameOverWidget::HandleChoiceEscape()
{
	// The root GameOver screen cannot be closed with Escape.
}

void UGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetImmediateSelectionMode(bImmediateSelections);

	// 레벨 선택 버튼들은 레벨 선택하기를 눌렀을 때만 보인다.
	if (PresetLoad)
	{
		PresetLoad->SetVisibility(ESlateVisibility::Collapsed);
	}
	HideStagePreview();
}

void UGameOverWidget::HandleContinueButtonClicked()
{
	SubmitSelection(EGameOverPendingChoice::Continue);
}

void UGameOverWidget::HandleLevelSelectButtonClicked()
{
	if (!PresetLoad)
	{
		return;
	}
	if (PresetLoad->IsVisible())
	{
		ClosePresetLoad();
		return;
	}
	PresetLoad->SetVisibility(ESlateVisibility::Visible);
	PresetLoad->SelectFirstStage();
}

void UGameOverWidget::ClosePresetLoad()
{
	if (!PresetLoad || !PresetLoad->IsVisible())
	{
		return;
	}
	PresetLoad->SetVisibility(ESlateVisibility::Collapsed);
	HideStagePreview();
	if (ChoiceMenu && GetOwningPlayer())
	{
		ChoiceMenu->SelectButton(EGameOverMenuChoice::SelectLevel);
	}
}

void UGameOverWidget::HandleMainMenuButtonClicked()
{
	SubmitSelection(EGameOverPendingChoice::MainMenu);
}

void UGameOverWidget::HandleQuitGameButtonClicked()
{
	if (bImmediateSelections)
	{
		SubmitSelection(EGameOverPendingChoice::QuitGame);
	}
}

void UGameOverWidget::HandlePresetStageConfirmed(FName StageId)
{
	(void)StageId;
	const int32 LevelIndex = PresetLoad
		? static_cast<int32>(PresetLoad->GetSelectedStage())
		: 0;
	if (LevelIndex >= 1 && LevelIndex <= 4)
	{
		SubmitSelection(EGameOverPendingChoice::PresetLevel, LevelIndex);
	}
}

void UGameOverWidget::SubmitSelection(EGameOverPendingChoice Choice, int32 LevelIndex)
{
	if (AFirstPersonPlayerController* Controller =
		Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
	{
		FGameOverPendingRequest Request;
		Request.Choice = Choice;
		Request.LevelIndex = LevelIndex;
		Controller->RequestGameOverPendingChoice(Request);
	}
}

void UGameOverWidget::HandlePresetStageHoverChanged(EOutlierStage Stage, bool bHovered)
{
	if (bHovered)
	{
		ShowStagePreview(Stage);
		return;
	}

	// 버튼 사이를 옮겨 갈 때 이전 버튼의 Unhovered가 늦게 와도 새 미리보기는 지우지 않는다.
	if (Stage == PreviewStage)
	{
		HideStagePreview();
	}
}

void UGameOverWidget::ShowStagePreview(EOutlierStage Stage)
{
	if (!StagePreviewImage)
	{
		return;
	}

	const TObjectPtr<UTexture2D>* PreviewTexture = StagePreviewTextures.Find(Stage);
	if (!PreviewTexture || !*PreviewTexture)
	{
		HideStagePreview();
		return;
	}

	// 크기는 WBP에서 잡은 이미지 크기를 유지한다.
	StagePreviewImage->SetBrushFromTexture(*PreviewTexture, false);
	// 이미지가 버튼과 겹쳐도 hover를 가로채지 않도록 HitTestInvisible로 띄운다.
	StagePreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewStage = Stage;
}

void UGameOverWidget::HideStagePreview()
{
	if (StagePreviewImage)
	{
		StagePreviewImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	PreviewStage = EOutlierStage::None;
}
