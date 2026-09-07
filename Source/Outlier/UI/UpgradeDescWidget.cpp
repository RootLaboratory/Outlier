#include "UI/UpgradeDescWidget.h"

#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "PopupRetainerBox.h"

void UUpgradeDescWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PopUpRetainer)
	{
		PopUpRetainer->OnClosed.AddUniqueDynamic(this, &UUpgradeDescWidget::HandlePopupClosed);
		PopUpRetainer->ResetPopup();
	}

	RefreshTextBlocks();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeDescWidget::InjectNodeData(
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	UpdateNodeData(NAME_None, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
}

void UUpgradeDescWidget::UpdateNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	CurrentNodeRowName = InNodeRowName;
	CurrentNodeData = InNodeData;
	CurrentNodeState = InNodeState;
	CurrentNodeCount = InCurrentNodeCount;
	bCanAfford = bInCanAfford;
	bClearWhenClosed = false;

	RefreshTextBlocks();
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();

	if (PopUpRetainer)
	{
		PopUpRetainer->RequestRender();
	}
}

void UUpgradeDescWidget::ShowNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	UpdateNodeData(InNodeRowName, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();
	PlayPopUp(true);
}

void UUpgradeDescWidget::HideNodeData(FName InNodeRowName)
{
	if (!InNodeRowName.IsNone() && !CurrentNodeRowName.IsNone() && InNodeRowName != CurrentNodeRowName)
	{
		return;
	}

	bClearWhenClosed = true;
	PlayPopUp(false);
}

void UUpgradeDescWidget::ClearNodeData()
{
	CurrentNodeRowName = NAME_None;
	CurrentNodeData = FOutlierUpgradeNodeRow();
	CurrentNodeState = EOutlierUpgradeNodeState::Locked;
	CurrentNodeCount = 0;
	bCanAfford = false;
	bClearWhenClosed = false;

	RefreshTextBlocks();

	if (PopUpRetainer)
	{
		PopUpRetainer->ResetPopup();
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeDescWidget::PlayPopUp(bool bOpen)
{
	if (bOpen)
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (PopUpRetainer)
	{
		bOpen ? PopUpRetainer->PlayOpen() : PopUpRetainer->PlayClose();
		return;
	}

	if (!bOpen)
	{
		HandlePopupClosed();
	}
}

void UUpgradeDescWidget::HandlePopupClosed()
{
	if (bClearWhenClosed)
	{
		CurrentNodeRowName = NAME_None;
		CurrentNodeData = FOutlierUpgradeNodeRow();
		CurrentNodeState = EOutlierUpgradeNodeState::Locked;
		CurrentNodeCount = 0;
		bCanAfford = false;
		bClearWhenClosed = false;
		RefreshTextBlocks();
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeDescWidget::RefreshTextBlocks()
{
	if (DescText)
	{
		DescText->SetText(CurrentNodeData.Desc);
	}

	if (CostText)
	{
		CostText->SetText(BuildCostText());
	}

	if (CostNeedText)
	{
		CostNeedText->SetText(BuildCostNeedText());
	}

	const FText StateText = BuildStateDescText();
	CurrentStateDescText = StateText;
	bShouldShowStateDescText = ShouldShowStateDescText(StateText);

	if (StateDescText)
	{
		StateDescText->SetText(StateText);
		StateDescText->SetVisibility(bShouldShowStateDescText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshStateDescSwitcher(StateText);

	RefreshCostTextStyle();
}

void UUpgradeDescWidget::RefreshCostTextStyle()
{
	const FSlateColor& CostColor = bCanAfford ? DefaultCostTextColor : InsufficientCostTextColor;

	if (CostNeedText)
	{
		CostNeedText->SetColorAndOpacity(CostColor);
	}

	if (CostText)
	{
		CostText->SetColorAndOpacity(CostColor);
	}
}

FText UUpgradeDescWidget::BuildCostNeedText() const
{
	FFormatOrderedArguments Arguments;
	Arguments.Add(FText::AsNumber(CurrentNodeData.Cost));
	Arguments.Add(FText::AsNumber(CurrentNodeCount));
	return FText::Format(CostNeedTextFormat, Arguments);
}

FText UUpgradeDescWidget::BuildCostText() const
{
	FFormatOrderedArguments Arguments;
	Arguments.Add(FText::AsNumber(CurrentNodeData.Cost));
	Arguments.Add(FText::AsNumber(CurrentNodeCount));
	return FText::Format(CostTextFormat, Arguments);
}

FText UUpgradeDescWidget::BuildStateDescText_Implementation() const
{
	if (CurrentNodeState == EOutlierUpgradeNodeState::Activated)
	{
		return ActivatedStateDescText;
	}

	if (CurrentNodeState == EOutlierUpgradeNodeState::Locked && !CurrentNodeData.ParentId.IsNone())
	{
		return LockedByParentStateDescText;
	}

	return DefaultStateDescText;
}

bool UUpgradeDescWidget::ShouldShowStateDescText(const FText& StateText) const
{
	return !StateText.IsEmpty();
}

void UUpgradeDescWidget::RefreshStateDescSwitcher(const FText& StateText)
{
	if (!StateDescSwitcher)
	{
		if (CostDescContent)
		{
			CostDescContent->SetVisibility(bShouldShowStateDescText ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
		if (StateDescContent)
		{
			StateDescContent->SetVisibility(bShouldShowStateDescText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}

		OnStateDescDisplayChanged(bShouldShowStateDescText, INDEX_NONE, StateText);
		return;
	}

	const int32 TargetIndex = bShouldShowStateDescText
		? StateDescSwitcherIndex
		: CostSwitcherIndex;

	UWidget* TargetWidget = bShouldShowStateDescText ? StateDescContent.Get() : CostDescContent.Get();
	if (!TargetWidget && bShouldShowStateDescText && StateDescText && StateDescText->GetParent() == StateDescSwitcher)
	{
		TargetWidget = StateDescText;
	}

	if (TargetWidget && TargetWidget->GetParent() == StateDescSwitcher)
	{
		StateDescSwitcher->SetActiveWidget(TargetWidget);
		OnStateDescDisplayChanged(bShouldShowStateDescText, StateDescSwitcher->GetActiveWidgetIndex(), StateText);
		return;
	}

	if (StateDescSwitcher->GetActiveWidgetIndex() != TargetIndex
		&& TargetIndex >= 0
		&& TargetIndex < StateDescSwitcher->GetNumWidgets())
	{
		StateDescSwitcher->SetActiveWidgetIndex(TargetIndex);
	}

	OnStateDescDisplayChanged(bShouldShowStateDescText, StateDescSwitcher->GetActiveWidgetIndex(), StateText);
}
