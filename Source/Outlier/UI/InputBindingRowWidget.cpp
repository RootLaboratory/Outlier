#include "UI/InputBindingRowWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/InputActionKeyDisplayWidget.h"

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

	SetDisplayedKey(Row.DefaultKey);

	if (InputBindButton)
	{
		InputBindButton->SetIsEnabled(Row.bRebindable);
		InputBindButton->OnClicked.AddUniqueDynamic(
			this,
			&UInputBindingRowWidget::HandleRebindButtonClicked);
		InputBindButton->OnHovered.AddUniqueDynamic(
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
	if (KeyDisplay)
	{
		KeyDisplay->SetDisplayedKey(NewKey);
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
