#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"

class STableViewBase;
class UInputAction;
class UInputMappingContext;

enum class EOutlierInputMappableInputType : uint8
{
	Keyboard,
	Mouse,
	Gamepad
};

struct FOutlierInputMappableRow
{
	bool bMakeMappable = false;
	// Read-only: whether this mapping is already mappable on the IMC (i.e.
	// already opted into the binding table by an earlier Apply), regardless
	// of the current bMakeMappable checkbox state.
	bool bInUse = false;
	TWeakObjectPtr<UInputMappingContext> MappingContext;
	TWeakObjectPtr<UInputAction> InputAction;
	FKey Key;
	int32 MappingIndex = INDEX_NONE;
	FName MappingName;
	FText DisplayName;
	EOutlierInputMappableInputType InputType = EOutlierInputMappableInputType::Keyboard;
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
	FReply FlushCheckedMappings();
	FReply CheckAllRows();
	FReply UncheckAllRows();

	void SetStatus(const FText& NewStatusText);
	void CollectConfiguredInputMappingContexts(TArray<UInputMappingContext*>& OutMappingContexts) const;

	ECheckBoxState GetShowKeyboardState() const;
	void OnShowKeyboardStateChanged(ECheckBoxState NewState);
	ECheckBoxState GetShowMouseState() const;
	void OnShowMouseStateChanged(ECheckBoxState NewState);
	void RefreshFilteredRows();

	TArray<FRowPtr> Rows;
	TArray<FRowPtr> FilteredRows;
	TSharedPtr<SListView<FRowPtr>> RowListView;
	FText StatusText;
	bool bShowKeyboardInputs = true;
	bool bShowMouseInputs = true;
};
