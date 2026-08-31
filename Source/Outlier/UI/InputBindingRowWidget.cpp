#include "UI/InputBindingRowWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UInputBindingRowWidget::InitializeInputBindingRow(
	const FOutlierInputBindingTableRow& InRow,
	int32 InRowIndex)
{
	Row = InRow;
	RowIndex = InRowIndex;

	if (ActionNameText)
	{
		ActionNameText->SetText(Row.DisplayName);
	}

	if (KeyText)
	{
		KeyText->SetText(Row.DefaultKey.GetDisplayName());
	}

	if (RebindButton)
	{
		RebindButton->SetIsEnabled(Row.bRebindable);
		RebindButton->OnClicked.AddUniqueDynamic(
			this,
			&UInputBindingRowWidget::HandleRebindButtonClicked);
		RebindButton->OnHovered.AddUniqueDynamic(
			this,
			&UInputBindingRowWidget::HandleRebindButtonHovered);
	}
}

const FOutlierInputBindingTableRow& UInputBindingRowWidget::GetInputBindingRow() const
{
	return Row;
}

int32 UInputBindingRowWidget::GetInputBindingRowIndex() const
{
	return RowIndex;
}

void UInputBindingRowWidget::SetDisplayedKey(FKey NewKey)
{
	if (KeyText)
	{
		KeyText->SetText(NewKey.GetDisplayName());
	}
}

void UInputBindingRowWidget::HandleRebindButtonClicked()
{
	OnRebindRequested.Broadcast(this, RowIndex);
}

void UInputBindingRowWidget::HandleRebindButtonHovered()
{
	OnRowHovered.Broadcast(this, RowIndex);
}
