#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SListViewBase;
class SProgressBar;
class STextBlock;

class SGraphicSettingDebuggerLogWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphicSettingDebuggerLogWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void RefreshSlots();
	void LoadSlot(const FString& SlotName);
	TSharedRef<ITableRow> MakeSlotRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FString>> Slots;
	TSharedPtr<SListView<TSharedPtr<FString>>> SlotList;
	TSharedPtr<STextBlock> DetailsText;
	TSharedPtr<SProgressBar> GpuBar;
	TSharedPtr<SProgressBar> FrameBar;
	TSharedPtr<SProgressBar> FpsBar;
};
