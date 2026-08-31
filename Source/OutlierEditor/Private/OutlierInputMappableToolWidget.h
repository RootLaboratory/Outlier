#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Widgets/SCompoundWidget.h"

class STableViewBase;
class UInputAction;
class UInputMappingContext;

struct FOutlierInputMappableRow
{
	bool bMakeMappable = false;
	TWeakObjectPtr<UInputMappingContext> MappingContext;
	TWeakObjectPtr<UInputAction> InputAction;
	FKey Key;
	int32 MappingIndex = INDEX_NONE;
	FName MappingName;
	FText DisplayName;
};

class SOutlierInputMappableToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOutlierInputMappableToolWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FRowPtr = TSharedPtr<FOutlierInputMappableRow>;

	TSharedRef<ITableRow> GenerateRow(
		FRowPtr Row,
		const TSharedRef<STableViewBase>& OwnerTable);

	FReply ScanInputMappingContexts();
	FReply ApplyCheckedMappings();
	FReply CheckAllRows();
	FReply UncheckAllRows();

	void SetStatus(const FText& NewStatusText);
	void CollectConfiguredInputMappingContexts(TArray<UInputMappingContext*>& OutMappingContexts) const;

	TArray<FRowPtr> Rows;
	TSharedPtr<SListView<FRowPtr>> RowListView;
	FText StatusText;
};
