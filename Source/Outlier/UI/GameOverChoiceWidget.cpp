#include "UI/GameOverChoiceWidget.h"

#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

UGameOverChoiceWidget::UGameOverChoiceWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UGameOverChoiceWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!ContinueButton || !LevelSelectButton || !MainMenuButton)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameOverChoice] Missing button binding on %s: Continue=%s LevelSelect=%s MainMenu=%s"),
			*GetNameSafe(this), *GetNameSafe(ContinueButton),
			*GetNameSafe(LevelSelectButton), *GetNameSafe(MainMenuButton));
	}

	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleContinueClicked);
		ContinueButton->OnHovered.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleContinueHovered);
	}
	if (LevelSelectButton)
	{
		LevelSelectButton->OnClicked.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleLevelSelectClicked);
		LevelSelectButton->OnHovered.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleLevelSelectHovered);
	}
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleMainMenuClicked);
		MainMenuButton->OnHovered.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleMainMenuHovered);
	}

	BindQuitGameButton();
	OriginalButtonStyles.Reset(4);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		OriginalButtonStyles.Add(GetButton(Index) ? GetButton(Index)->GetStyle() : FButtonStyle());
	}
}

void UGameOverChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetQuitGameAvailable(bQuitGameAvailable);
}

void UGameOverChoiceWidget::CreateQuitGameButton()
{
	if (QuitGameButton || !MainMenuButton || !WidgetTree)
	{
		return;
	}
	// 기존 3버튼 WBP도 리슨에서 바로 쓸 수 있도록 같은 VerticalBox 끝에 추가한다.
	UWidget* MainMenuBranch = MainMenuButton;
	UPanelWidget* Parent = MainMenuBranch->GetParent();
	while (Parent && !Parent->CanHaveMultipleChildren())
	{
		MainMenuBranch = Parent;
		Parent = MainMenuBranch->GetParent();
	}
	UVerticalBox* ButtonList = Cast<UVerticalBox>(Parent);
	if (!ButtonList)
	{
		return;
	}

	QuitGameButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuitGameButton"));
	// 화면에 적용된 선택 스타일이 아니라 WBP 원본 스타일을 복사한다.
	QuitGameButton->SetStyle(OriginalButtonStyles.IsValidIndex(2)
		? OriginalButtonStyles[2] : MainMenuButton->GetStyle());
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuitGameLabel"));
	TArray<UWidget*> MainMenuWidgets;
	UWidgetTree::GetChildWidgets(MainMenuButton, MainMenuWidgets);
	for (UWidget* Widget : MainMenuWidgets)
	{
		if (const UTextBlock* SourceLabel = Cast<UTextBlock>(Widget))
		{
			Label->SetFont(SourceLabel->GetFont());
			Label->SetColorAndOpacity(SourceLabel->GetColorAndOpacity());
			Label->SetShadowColorAndOpacity(SourceLabel->GetShadowColorAndOpacity());
			Label->SetShadowOffset(SourceLabel->GetShadowOffset());
			break;
		}
	}
	Label->SetText(NSLOCTEXT("GameOver", "QuitGame", "게임 종료"));
	Label->SetJustification(ETextJustify::Center);
	QuitGameButton->AddChild(Label, MainMenuButton->GetContent() ? MainMenuButton->GetContent()->Slot : nullptr);
	ButtonList->AddChild(QuitGameButton, MainMenuBranch->Slot);
	OriginalButtonStyles[3] = QuitGameButton->GetStyle();
	BindQuitGameButton();
}

void UGameOverChoiceWidget::BindQuitGameButton()
{
	if (QuitGameButton)
	{
		QuitGameButton->OnClicked.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleQuitGameClicked);
		QuitGameButton->OnHovered.AddUniqueDynamic(this, &UGameOverChoiceWidget::HandleQuitGameHovered);
	}
}

void UGameOverChoiceWidget::SetQuitGameAvailable(bool bAvailable)
{
	bQuitGameAvailable = bAvailable;
	if (bAvailable)
	{
		CreateQuitGameButton();
	}
	if (QuitGameButton)
	{
		QuitGameButton->SetVisibility(bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (ChosenIndex >= GetButtonCount())
	{
		ChosenIndex = 0;
	}
	ApplySelection();
}

int32 UGameOverChoiceWidget::GetButtonCount() const
{
	return bQuitGameAvailable && QuitGameButton ? 4 : 3;
}

EGameOverMenuChoice UGameOverChoiceWidget::GetChosenButton() const
{
	return static_cast<EGameOverMenuChoice>(ChosenIndex);
}

void UGameOverChoiceWidget::SelectButton(EGameOverMenuChoice Choice)
{
	SetChosenIndex(static_cast<int32>(Choice), true);
}

bool UGameOverChoiceWidget::HandleUILayerConfirmed_Implementation()
{
	ConfirmChoice(GetChosenButton());
	return true;
}

bool UGameOverChoiceWidget::HandleUILayerEscape_Implementation()
{
	OnEscapeRequested.Broadcast();
	return true;
}

bool UGameOverChoiceWidget::HandleUILayerUp_Implementation()
{
	MoveSelection(-1);
	return true;
}

bool UGameOverChoiceWidget::HandleUILayerDown_Implementation()
{
	MoveSelection(1);
	return true;
}

bool UGameOverChoiceWidget::HandleUILayerLeft_Implementation()
{
	// The main menu only uses Up/Down to change the chosen button.
	return true;
}

bool UGameOverChoiceWidget::HandleUILayerRight_Implementation()
{
	if (GetChosenButton() == EGameOverMenuChoice::SelectLevel)
	{
		ConfirmChoice(EGameOverMenuChoice::SelectLevel);
	}
	return true;
}

void UGameOverChoiceWidget::HandleContinueClicked()
{
	SetChosenIndex(0, true);
	ConfirmChoice(EGameOverMenuChoice::Continue);
}

void UGameOverChoiceWidget::HandleLevelSelectClicked()
{
	SetChosenIndex(1, true);
	ConfirmChoice(EGameOverMenuChoice::SelectLevel);
}

void UGameOverChoiceWidget::HandleMainMenuClicked()
{
	SetChosenIndex(2, true);
	ConfirmChoice(EGameOverMenuChoice::MainMenu);
}

void UGameOverChoiceWidget::HandleQuitGameClicked()
{
	if (bQuitGameAvailable)
	{
		SetChosenIndex(3, true);
		ConfirmChoice(EGameOverMenuChoice::QuitGame);
	}
}

void UGameOverChoiceWidget::HandleContinueHovered()
{
	SetChosenIndex(0, false);
}

void UGameOverChoiceWidget::HandleLevelSelectHovered()
{
	SetChosenIndex(1, false);
}

void UGameOverChoiceWidget::HandleMainMenuHovered()
{
	SetChosenIndex(2, false);
}

void UGameOverChoiceWidget::HandleQuitGameHovered()
{
	if (bQuitGameAvailable)
	{
		SetChosenIndex(3, false);
	}
}

void UGameOverChoiceWidget::MoveSelection(int32 Step)
{
	const int32 Count = GetButtonCount();
	SetChosenIndex((ChosenIndex + Step + Count) % Count, true);
}

void UGameOverChoiceWidget::SetChosenIndex(int32 Index, bool bFocusButton)
{
	if (Index < 0 || Index >= GetButtonCount())
	{
		return;
	}
	ChosenIndex = Index;
	ApplySelection();
	if (bFocusButton && GetOwningPlayer())
	{
		SetUserFocus(GetOwningPlayer());
	}
}

void UGameOverChoiceWidget::ApplySelection()
{
	for (int32 Index = 0; Index < GetButtonCount(); ++Index)
	{
		UButton* Button = GetButton(Index);
		if (!Button || !OriginalButtonStyles.IsValidIndex(Index))
		{
			continue;
		}
		FButtonStyle Style = OriginalButtonStyles[Index];
		if (Index == ChosenIndex)
		{
			Style.SetNormal(Style.Hovered);
		}
		else
		{
			Style.SetHovered(Style.Normal);
		}
		Button->SetStyle(Style);
	}
}

void UGameOverChoiceWidget::ConfirmChoice(EGameOverMenuChoice Choice)
{
	UE_LOG(LogTemp, Log, TEXT("[GameOverChoice] Confirm %d on %s"),
		static_cast<int32>(Choice), *GetNameSafe(this));
	OnChoiceConfirmed.Broadcast(Choice);
}

UButton* UGameOverChoiceWidget::GetButton(int32 Index) const
{
	switch (Index)
	{
	case 0: return ContinueButton;
	case 1: return LevelSelectButton;
	case 2: return MainMenuButton;
	case 3: return QuitGameButton;
	default: return nullptr;
	}
}
