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

void UInputBindingRowWidget::SetBindingError(const FText& Error)
{
	// 기존 WBP 레이아웃을 유지하면서 불일치 행을 표시한다. 상세 원인은 버튼 툴팁으로 제공한다.
	if (ActionNameText)
	{
		ActionNameText->SetText(Error.IsEmpty() ? Row.DisplayName
			: FText::Format(NSLOCTEXT("SettingInput", "LinkedBindingError", "{0} (연결 확인 필요)"), Row.DisplayName));
	}
	if (InputBindButton) InputBindButton->SetToolTipText(Error);
}

void UInputBindingRowWidget::HandleRebindButtonClicked()
{
	OnRebindRequested.Broadcast(this, RowIndex);
}

void UInputBindingRowWidget::HandleRebindButtonHovered()
{
	OnRowHovered.Broadcast(this, RowIndex);
}
