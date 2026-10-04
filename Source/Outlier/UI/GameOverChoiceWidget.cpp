#include "UI/GameOverChoiceWidget.h"

#include "Components/Button.h"

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

	OriginalButtonStyles.Reset(3);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OriginalButtonStyles.Add(GetButton(Index) ? GetButton(Index)->GetStyle() : FButtonStyle());
	}
}

void UGameOverChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplySelection();
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

void UGameOverChoiceWidget::MoveSelection(int32 Step)
{
	SetChosenIndex((ChosenIndex + Step + 3) % 3, true);
}

void UGameOverChoiceWidget::SetChosenIndex(int32 Index, bool bFocusButton)
{
	if (Index < 0 || Index >= 3)
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
	for (int32 Index = 0; Index < 3; ++Index)
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
	default: return nullptr;
	}
}
