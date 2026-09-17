// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/PreSetLoadWidget.h"

#include "Components/Button.h"

void UPreSetLoadWidget::NativeConstruct()
{
	Super::NativeConstruct();

	StageButtons = { UnPresetButton, Level01Button, Level02Button, Level03Button, Level04Button };

	if (UnPresetButton)
	{
		UnPresetButton->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleUnPresetButtonClicked);
	}

	if (Level01Button)
	{
		Level01Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel01ButtonClicked);
	}

	if (Level02Button)
	{
		Level02Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel02ButtonClicked);
	}

	if (Level03Button)
	{
		Level03Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel03ButtonClicked);
	}

	if (Level04Button)
	{
		Level04Button->OnClicked.AddUniqueDynamic(this, &UPreSetLoadWidget::HandleLevel04ButtonClicked);
	}
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
	HandleStageButtonClicked(EOutlierStage::Level01);
}

void UPreSetLoadWidget::HandleLevel02ButtonClicked()
{
	HandleStageButtonClicked(EOutlierStage::Level02);
}

void UPreSetLoadWidget::HandleLevel03ButtonClicked()
{
	HandleStageButtonClicked(EOutlierStage::Level03);
}

void UPreSetLoadWidget::HandleLevel04ButtonClicked()
{
	HandleStageButtonClicked(EOutlierStage::Level04);
}
