#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/OutlierInputBindingTable.h"
#include "InputBindingRowWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnInputBindingRowEvent,
	UInputBindingRowWidget*,
	RowWidget,
	int32,
	RowIndex);

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInputBindingRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Input Binding")
	void InitializeInputBindingRow(const FOutlierInputBindingTableRow& InRow, int32 InRowIndex);

	UFUNCTION(BlueprintPure, Category = "Input Binding")
	const FOutlierInputBindingTableRow& GetInputBindingRow() const;

	UFUNCTION(BlueprintPure, Category = "Input Binding")
	int32 GetInputBindingRowIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Input Binding")
	void SetDisplayedKey(FKey NewKey);

	UPROPERTY(BlueprintAssignable, Category = "Input Binding")
	FOnInputBindingRowEvent OnRebindRequested;

	UPROPERTY(BlueprintAssignable, Category = "Input Binding")
	FOnInputBindingRowEvent OnRowHovered;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Binding")
	TObjectPtr<UTextBlock> ActionNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Binding")
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Input Binding")
	TObjectPtr<UButton> RebindButton;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	FOutlierInputBindingTableRow Row;

	UPROPERTY(BlueprintReadOnly, Category = "Input Binding")
	int32 RowIndex = INDEX_NONE;

private:
	UFUNCTION()
	void HandleRebindButtonClicked();

	UFUNCTION()
	void HandleRebindButtonHovered();
};
