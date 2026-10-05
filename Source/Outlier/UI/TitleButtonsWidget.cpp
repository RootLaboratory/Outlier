#include "UI/TitleButtonsWidget.h"

#include "Components/Button.h"

namespace
{
	constexpr int32 TitleButtonCount = static_cast<int32>(ETitleButtonAction::Exit) + 1;
}

void UTitleButtonsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (StartButton)
	{
		StartButton->OnClicked.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleStartClicked);
		StartButton->OnHovered.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleStartHovered);
	}
	if (CreditButton)
	{
		CreditButton->OnClicked.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleCreditClicked);
		CreditButton->OnHovered.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleCreditHovered);
	}
	if (SettingButton)
	{
		SettingButton->OnClicked.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleSettingClicked);
		SettingButton->OnHovered.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleSettingHovered);
	}
	if (ExitButton)
	{
		ExitButton->OnClicked.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleExitClicked);
		ExitButton->OnHovered.AddUniqueDynamic(this, &UTitleButtonsWidget::HandleExitHovered);
	}

	OriginalButtonStyles.Reset(TitleButtonCount);
	for (int32 Index = 0; Index < TitleButtonCount; ++Index)
	{
		const UButton* Button = GetButton(static_cast<ETitleButtonAction>(Index));
		OriginalButtonStyles.Add(Button ? Button->GetStyle() : FButtonStyle());
	}
}

void UTitleButtonsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplySelection();
}

bool UTitleButtonsWidget::HandleUILayerEscape_Implementation()
{
	// Escape는 Exit 버튼을 누른 것과 같다.
	SelectAction(ETitleButtonAction::Exit);
	ConfirmAction(ETitleButtonAction::Exit);
	return true;
}

bool UTitleButtonsWidget::HandleUILayerConfirmed_Implementation()
{
	ConfirmAction(SelectedAction);
	return true;
}

bool UTitleButtonsWidget::HandleUILayerUp_Implementation()
{
	MoveSelection(-1);
	return true;
}

bool UTitleButtonsWidget::HandleUILayerDown_Implementation()
{
	MoveSelection(1);
	return true;
}

bool UTitleButtonsWidget::HandleUILayerLeft_Implementation()
{
	// 세로 메뉴라 좌우 입력은 쓰지 않는다.
	return false;
}

bool UTitleButtonsWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UTitleButtonsWidget::HandleStartClicked()
{
	SelectAction(ETitleButtonAction::Start);
	ConfirmAction(ETitleButtonAction::Start);
}

void UTitleButtonsWidget::HandleCreditClicked()
{
	SelectAction(ETitleButtonAction::Credit);
	ConfirmAction(ETitleButtonAction::Credit);
}

void UTitleButtonsWidget::HandleSettingClicked()
{
	SelectAction(ETitleButtonAction::Setting);
	ConfirmAction(ETitleButtonAction::Setting);
}

void UTitleButtonsWidget::HandleExitClicked()
{
	SelectAction(ETitleButtonAction::Exit);
	ConfirmAction(ETitleButtonAction::Exit);
}

void UTitleButtonsWidget::HandleStartHovered()
{
	SelectAction(ETitleButtonAction::Start);
}

void UTitleButtonsWidget::HandleCreditHovered()
{
	SelectAction(ETitleButtonAction::Credit);
}

void UTitleButtonsWidget::HandleSettingHovered()
{
	SelectAction(ETitleButtonAction::Setting);
}

void UTitleButtonsWidget::HandleExitHovered()
{
	SelectAction(ETitleButtonAction::Exit);
}

void UTitleButtonsWidget::MoveSelection(int32 Step)
{
	const int32 Index = (static_cast<int32>(SelectedAction) + Step + TitleButtonCount) % TitleButtonCount;
	SelectAction(static_cast<ETitleButtonAction>(Index));
}

void UTitleButtonsWidget::SelectAction(ETitleButtonAction Action)
{
	if (SelectedAction == Action)
	{
		return;
	}

	SelectedAction = Action;
	ApplySelection();
}

void UTitleButtonsWidget::ApplySelection()
{
	for (int32 Index = 0; Index < TitleButtonCount; ++Index)
	{
		UButton* Button = GetButton(static_cast<ETitleButtonAction>(Index));
		if (!Button || !OriginalButtonStyles.IsValidIndex(Index))
		{
			continue;
		}

		// 키보드 선택과 마우스 Hover가 같은 강조로 보이도록 선택 버튼만 Hovered 모양을 쓴다.
		FButtonStyle Style = OriginalButtonStyles[Index];
		if (Index == static_cast<int32>(SelectedAction))
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

void UTitleButtonsWidget::ConfirmAction(ETitleButtonAction Action)
{
	OnActionConfirmed.Broadcast(Action);
}

UButton* UTitleButtonsWidget::GetButton(ETitleButtonAction Action) const
{
	switch (Action)
	{
	case ETitleButtonAction::Start: return StartButton;
	case ETitleButtonAction::Credit: return CreditButton;
	case ETitleButtonAction::Setting: return SettingButton;
	case ETitleButtonAction::Exit: return ExitButton;
	default: return nullptr;
	}
}
