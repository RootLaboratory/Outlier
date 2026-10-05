#include "UI/InGameSettingButtonsWidget.h"

#include "Components/Button.h"

namespace
{
	constexpr int32 ButtonCount = static_cast<int32>(EInGameSettingButtonAction::Exit) + 1;
}

void UInGameSettingButtonsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!ContinueButton || !RestartCheckpointButton || !SettingButton
		|| !TitleButton || !ExitButton)
	{
		return;
	}

	GetButton(EInGameSettingButtonAction::Continue)->OnClicked.AddUniqueDynamic(
		this, &ThisClass::HandleContinueClicked);
	GetButton(EInGameSettingButtonAction::Continue)->OnHovered.AddUniqueDynamic(
		this, &ThisClass::HandleContinueHovered);
	GetButton(EInGameSettingButtonAction::RestartCheckpoint)->OnClicked.AddUniqueDynamic(
		this, &ThisClass::HandleRestartCheckpointClicked);
	GetButton(EInGameSettingButtonAction::RestartCheckpoint)->OnHovered.AddUniqueDynamic(
		this, &ThisClass::HandleRestartCheckpointHovered);
	GetButton(EInGameSettingButtonAction::Setting)->OnClicked.AddUniqueDynamic(
		this, &ThisClass::HandleSettingClicked);
	GetButton(EInGameSettingButtonAction::Setting)->OnHovered.AddUniqueDynamic(
		this, &ThisClass::HandleSettingHovered);
	GetButton(EInGameSettingButtonAction::Title)->OnClicked.AddUniqueDynamic(
		this, &ThisClass::HandleTitleClicked);
	GetButton(EInGameSettingButtonAction::Title)->OnHovered.AddUniqueDynamic(
		this, &ThisClass::HandleTitleHovered);
	GetButton(EInGameSettingButtonAction::Exit)->OnClicked.AddUniqueDynamic(
		this, &ThisClass::HandleExitClicked);
	GetButton(EInGameSettingButtonAction::Exit)->OnHovered.AddUniqueDynamic(
		this, &ThisClass::HandleExitHovered);

	OriginalButtonStyles.SetNum(ButtonCount);
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		OriginalButtonStyles[Index] = GetButton(
			static_cast<EInGameSettingButtonAction>(Index))->GetStyle();
	}
}

void UInGameSettingButtonsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResetSelection();
}

void UInGameSettingButtonsWidget::ResetSelection()
{
	SelectedAction = EInGameSettingButtonAction::Continue;
	ApplySelection();
}

void UInGameSettingButtonsWidget::SetActionEnabled(
	EInGameSettingButtonAction Action, bool bEnabled)
{
	if (UButton* Button = GetButton(Action))
	{
		Button->SetIsEnabled(bEnabled);
	}
	if (!IsActionEnabled(SelectedAction))
	{
		MoveSelection(1);
	}
	ApplySelection();
}

bool UInGameSettingButtonsWidget::HandleUILayerEscape_Implementation()
{
	SelectAction(EInGameSettingButtonAction::Continue);
	ConfirmAction(EInGameSettingButtonAction::Continue);
	return true;
}

bool UInGameSettingButtonsWidget::HandleUILayerConfirmed_Implementation()
{
	ConfirmAction(SelectedAction);
	return true;
}

bool UInGameSettingButtonsWidget::HandleUILayerUp_Implementation()
{
	MoveSelection(-1);
	return true;
}

bool UInGameSettingButtonsWidget::HandleUILayerDown_Implementation()
{
	MoveSelection(1);
	return true;
}

bool UInGameSettingButtonsWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool UInGameSettingButtonsWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UInGameSettingButtonsWidget::HandleContinueClicked()
{
	SelectAction(EInGameSettingButtonAction::Continue);
	ConfirmAction(EInGameSettingButtonAction::Continue);
}

void UInGameSettingButtonsWidget::HandleRestartCheckpointClicked()
{
	SelectAction(EInGameSettingButtonAction::RestartCheckpoint);
	ConfirmAction(EInGameSettingButtonAction::RestartCheckpoint);
}

void UInGameSettingButtonsWidget::HandleSettingClicked()
{
	SelectAction(EInGameSettingButtonAction::Setting);
	ConfirmAction(EInGameSettingButtonAction::Setting);
}

void UInGameSettingButtonsWidget::HandleTitleClicked()
{
	SelectAction(EInGameSettingButtonAction::Title);
	ConfirmAction(EInGameSettingButtonAction::Title);
}

void UInGameSettingButtonsWidget::HandleExitClicked()
{
	SelectAction(EInGameSettingButtonAction::Exit);
	ConfirmAction(EInGameSettingButtonAction::Exit);
}

void UInGameSettingButtonsWidget::HandleContinueHovered()
{
	SelectAction(EInGameSettingButtonAction::Continue);
}

void UInGameSettingButtonsWidget::HandleRestartCheckpointHovered()
{
	SelectAction(EInGameSettingButtonAction::RestartCheckpoint);
}

void UInGameSettingButtonsWidget::HandleSettingHovered()
{
	SelectAction(EInGameSettingButtonAction::Setting);
}

void UInGameSettingButtonsWidget::HandleTitleHovered()
{
	SelectAction(EInGameSettingButtonAction::Title);
}

void UInGameSettingButtonsWidget::HandleExitHovered()
{
	SelectAction(EInGameSettingButtonAction::Exit);
}

void UInGameSettingButtonsWidget::MoveSelection(int32 Step)
{
	int32 Index = static_cast<int32>(SelectedAction);
	for (int32 Attempt = 0; Attempt < ButtonCount; ++Attempt)
	{
		Index = (Index + Step + ButtonCount) % ButtonCount;
		const EInGameSettingButtonAction Candidate =
			static_cast<EInGameSettingButtonAction>(Index);
		if (IsActionEnabled(Candidate))
		{
			SelectAction(Candidate);
			return;
		}
	}
}

void UInGameSettingButtonsWidget::SelectAction(EInGameSettingButtonAction Action)
{
	if (!IsActionEnabled(Action) || SelectedAction == Action)
	{
		return;
	}
	SelectedAction = Action;
	ApplySelection();
}

void UInGameSettingButtonsWidget::ApplySelection()
{
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		UButton* Button = GetButton(
			static_cast<EInGameSettingButtonAction>(Index));
		if (!Button || !OriginalButtonStyles.IsValidIndex(Index))
		{
			continue;
		}
		FButtonStyle Style = OriginalButtonStyles[Index];
		if (Index == static_cast<int32>(SelectedAction) && Button->GetIsEnabled())
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

void UInGameSettingButtonsWidget::ConfirmAction(EInGameSettingButtonAction Action)
{
	if (IsActionEnabled(Action))
	{
		OnActionConfirmed.Broadcast(Action);
	}
}

UButton* UInGameSettingButtonsWidget::GetButton(EInGameSettingButtonAction Action) const
{
	switch (Action)
	{
	case EInGameSettingButtonAction::Continue: return ContinueButton;
	case EInGameSettingButtonAction::RestartCheckpoint: return RestartCheckpointButton;
	case EInGameSettingButtonAction::Setting: return SettingButton;
	case EInGameSettingButtonAction::Title: return TitleButton;
	case EInGameSettingButtonAction::Exit: return ExitButton;
	default: return nullptr;
	}
}

bool UInGameSettingButtonsWidget::IsActionEnabled(EInGameSettingButtonAction Action) const
{
	const UButton* Button = GetButton(Action);
	return Button && Button->GetIsEnabled();
}
