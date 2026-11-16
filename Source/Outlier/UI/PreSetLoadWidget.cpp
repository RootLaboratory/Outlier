#include "UI/PreSetLoadWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/Button.h"

void UPreSetLoadWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	// 목록 전체를 벗어난 경우에만 이탈을 알린다. 목록 안에서 버튼 사이를 이동할 때는 열린 상태를 유지한다.
	OnPresetMouseLeft.Broadcast();
}

void UPreSetLoadWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	StageButtons = { UnPresetButton, Level01Button, Level02Button, Level03Button, Level04Button };
	if (OriginalStageButtonStyles.IsEmpty())
	{
		for (int32 Index = 0; Index < StageButtons.Num(); ++Index)
		{
			OriginalStageButtonStyles.Add(StageButtons[Index]
				? StageButtons[Index]->GetStyle() : FButtonStyle());
		}
	}

	if (UnPresetButton)
	{
		UnPresetButton->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleUnPresetButtonClicked);
	}

	if (Level01Button)
	{
		Level01Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel01ButtonClicked);
		Level01Button->OnHovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel01ButtonHovered);
		Level01Button->OnUnhovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel01ButtonUnhovered);
	}

	if (Level02Button)
	{
		Level02Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel02ButtonClicked);
		Level02Button->OnHovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel02ButtonHovered);
		Level02Button->OnUnhovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel02ButtonUnhovered);
	}

	if (Level03Button)
	{
		Level03Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel03ButtonClicked);
		Level03Button->OnHovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel03ButtonHovered);
		Level03Button->OnUnhovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel03ButtonUnhovered);
	}

	if (Level04Button)
	{
		Level04Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel04ButtonClicked);
		Level04Button->OnHovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel04ButtonHovered);
		Level04Button->OnUnhovered.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel04ButtonUnhovered);
	}
	ApplyStageSelection();
}

void UPreSetLoadWidget::SelectFirstStage()
{
	SetChosenStage(EOutlierStage::Level01, true);
}

void UPreSetLoadWidget::ResetStageSelection()
{
	SelectedStage = EOutlierStage::None;
	ChosenStageIndex = 0;
	bShowStageSelection = false;
	ApplyStageSelection();
}

void UPreSetLoadWidget::SetRetainSelectedStage(bool bRetain)
{
	bRetainSelectedStage = bRetain;
	ApplyStageSelection();
}

void UPreSetLoadWidget::MoveStageSelection(int32 Step)
{
	const int32 Direction = Step < 0 ? -1 : 1;
	const int32 PreviousIndex = ChosenStageIndex;
	int32 NextIndex = (ChosenStageIndex + Step % 4 + 4) % 4;
	for (int32 Checked = 0; Checked < 4; ++Checked)
	{
		const EOutlierStage Stage = static_cast<EOutlierStage>(NextIndex + 1);
		if (GetStageButton(Stage))
		{
			SetChosenStage(Stage, true);
			if (ChosenStageIndex != PreviousIndex) OutlierAudioFeedback::MenuNavigate(this);
			return;
		}
		// 선택 사항인 스테이지 버튼이 없어도 키보드 선택이 해당 항목에 갇히지 않도록 건너뛴다.
		NextIndex = (NextIndex + Direction + 4) % 4;
	}
}

bool UPreSetLoadWidget::ConfirmStageSelection()
{
	const EOutlierStage Stage = static_cast<EOutlierStage>(ChosenStageIndex + 1);
	if (!GetStageButton(Stage))
	{
		return false;
	}
	HandleStageButtonClicked(Stage);
	return true;
}

void UPreSetLoadWidget::SetChosenStage(EOutlierStage Stage, bool bFocusButton)
{
	const int32 StageIndex = static_cast<int32>(Stage) - 1;
	if (StageIndex < 0 || StageIndex >= 4 || !GetStageButton(Stage))
	{
		return;
	}
	ChosenStageIndex = StageIndex;
	bShowStageSelection = true;
	ApplyStageSelection();
	OnPresetStageHoverChanged.Broadcast(Stage, true);
	if (bFocusButton && GetOwningPlayer())
	{
		SetUserFocus(GetOwningPlayer());
	}
}

void UPreSetLoadWidget::HandleStageButtonUnhovered(EOutlierStage Stage)
{
	// 이전 버튼의 호버 해제 알림이 늦게 도착해도 새 선택의 강조 표시는 유지한다.
	if (static_cast<int32>(Stage) - 1 == ChosenStageIndex)
	{
		bShowStageSelection = false;
		ApplyStageSelection();
	}
	// GameOver는 이 알림을 별도의 StagePreviewWidget에 전달해 미리보기 전체를 숨긴다.
	OnPresetStageHoverChanged.Broadcast(Stage, false);
}

void UPreSetLoadWidget::ApplyStageSelection()
{
	for (int32 Index = 1; Index <= 4; ++Index)
	{
		UButton* Button = StageButtons.IsValidIndex(Index) ? StageButtons[Index] : nullptr;
		if (!Button || !OriginalStageButtonStyles.IsValidIndex(Index))
		{
			continue;
		}
		FButtonStyle Style = OriginalStageButtonStyles[Index];
		if ((bShowStageSelection && Index == ChosenStageIndex + 1)
			|| (bRetainSelectedStage && Index == static_cast<int32>(SelectedStage)))
		{
			Style.SetNormal(Style.Hovered);
		}
		else if (bShowStageSelection)
		{
			Style.SetHovered(Style.Normal);
		}
		Button->SetStyle(Style);
	}
}

UButton* UPreSetLoadWidget::GetStageButton(EOutlierStage Stage) const
{
	const int32 Index = static_cast<int32>(Stage);
	return StageButtons.IsValidIndex(Index) ? StageButtons[Index] : nullptr;
}

void UPreSetLoadWidget::HandleUnPresetButtonClicked()
{
	HandleStageButtonClicked(EOutlierStage::None);
}

void UPreSetLoadWidget::HandleStageButtonClicked(EOutlierStage Stage)
{
	const int32 StageIndex = static_cast<int32>(Stage);
	if (StageButtons.IsValidIndex(StageIndex) && StageButtons[StageIndex])
	{
		SelectedStage = Stage;
		if (bRetainSelectedStage)
		{
			ApplyStageSelection();
		}
		OnPresetStageConfirmed.Broadcast(GetSelectedStageId());
	}
}

FName UPreSetLoadWidget::GetSelectedStageId() const
{
	switch (SelectedStage)
	{
	case EOutlierStage::Level01: return OutlierPresetStageIds::Level1;
	case EOutlierStage::Level02: return OutlierPresetStageIds::Level2;
	case EOutlierStage::Level03: return OutlierPresetStageIds::Level3;
	case EOutlierStage::Level04: return OutlierPresetStageIds::Level4;
	default: return OutlierPresetStageIds::Start;
	}
}

void UPreSetLoadWidget::HandleLevel01ButtonClicked()
{
	SetChosenStage(EOutlierStage::Level01, true);
	HandleStageButtonClicked(EOutlierStage::Level01);
}

void UPreSetLoadWidget::HandleLevel02ButtonClicked()
{
	SetChosenStage(EOutlierStage::Level02, true);
	HandleStageButtonClicked(EOutlierStage::Level02);
}

void UPreSetLoadWidget::HandleLevel03ButtonClicked()
{
	SetChosenStage(EOutlierStage::Level03, true);
	HandleStageButtonClicked(EOutlierStage::Level03);
}

void UPreSetLoadWidget::HandleLevel04ButtonClicked()
{
	SetChosenStage(EOutlierStage::Level04, true);
	HandleStageButtonClicked(EOutlierStage::Level04);
}

void UPreSetLoadWidget::HandleLevel01ButtonHovered()
{
	SetChosenStage(EOutlierStage::Level01, false);
}

void UPreSetLoadWidget::HandleLevel01ButtonUnhovered()
{
	HandleStageButtonUnhovered(EOutlierStage::Level01);
}

void UPreSetLoadWidget::HandleLevel02ButtonHovered()
{
	SetChosenStage(EOutlierStage::Level02, false);
}

void UPreSetLoadWidget::HandleLevel02ButtonUnhovered()
{
	HandleStageButtonUnhovered(EOutlierStage::Level02);
}

void UPreSetLoadWidget::HandleLevel03ButtonHovered()
{
	SetChosenStage(EOutlierStage::Level03, false);
}

void UPreSetLoadWidget::HandleLevel03ButtonUnhovered()
{
	HandleStageButtonUnhovered(EOutlierStage::Level03);
}

void UPreSetLoadWidget::HandleLevel04ButtonHovered()
{
	SetChosenStage(EOutlierStage::Level04, false);
}

void UPreSetLoadWidget::HandleLevel04ButtonUnhovered()
{
	HandleStageButtonUnhovered(EOutlierStage::Level04);
}
