#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UpgradeDescWidget.generated.h"

class UTextBlock;
class UPopupRetainerBox;
class UWidget;
class UWidgetSwitcher;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UUpgradeDescWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void InjectNodeData(
		const FOutlierUpgradeNodeRow& InNodeData,
		EOutlierUpgradeNodeState InNodeState,
		int32 InCurrentNodeCount,
		bool bInCanAfford);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void UpdateNodeData(
		FName InNodeRowName,
		const FOutlierUpgradeNodeRow& InNodeData,
		EOutlierUpgradeNodeState InNodeState,
		int32 InCurrentNodeCount,
		bool bInCanAfford);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void ShowNodeData(
		FName InNodeRowName,
		const FOutlierUpgradeNodeRow& InNodeData,
		EOutlierUpgradeNodeState InNodeState,
		int32 InCurrentNodeCount,
		bool bInCanAfford);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void HideNodeData(FName InNodeRowName);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void ClearNodeData();

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void PlayPopUp(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	FOutlierUpgradeNodeRow GetCurrentNodeData() const { return CurrentNodeData; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	FName GetCurrentNodeRowName() const { return CurrentNodeRowName; }

	UFUNCTION(BlueprintPure, Category = "Upgrade|State")
	bool ShouldShowCurrentStateDescText() const { return bShouldShowStateDescText; }

	UFUNCTION(BlueprintPure, Category = "Upgrade|State")
	FText GetCurrentStateDescText() const { return CurrentStateDescText; }

protected:
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Upgrade|State")
	FText BuildStateDescText() const;
	virtual FText BuildStateDescText_Implementation() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Upgrade|State")
	void OnStateDescDisplayChanged(bool bInShouldShowStateDescText, int32 ActiveSwitcherIndex, const FText& StateText);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade")
	TObjectPtr<UPopupRetainerBox> PopUpRetainer;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FName CurrentNodeRowName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FOutlierUpgradeNodeRow CurrentNodeData;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeNodeState CurrentNodeState = EOutlierUpgradeNodeState::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	int32 CurrentNodeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	bool bCanAfford = false;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade|State")
	bool bShouldShowStateDescText = false;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade|State")
	FText CurrentStateDescText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DescText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> CostNeedText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> CostText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StateDescText;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> StateDescSwitcher;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CostDescContent;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> StateDescContent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FSlateColor DefaultCostTextColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FSlateColor InsufficientCostTextColor = FSlateColor(FLinearColor(1.0f, 0.1f, 0.1f, 1.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FText CostNeedTextFormat = NSLOCTEXT("UpgradeDescWidget", "CostNeedTextFormat", "{0}");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FText CostTextFormat = NSLOCTEXT("UpgradeDescWidget", "CostTextFormat", "{1}");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|State")
	FText ActivatedStateDescText = NSLOCTEXT("UpgradeDescWidget", "ActivatedStateDescText", "업그레이드 완료");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|State")
	FText LockedByParentStateDescText = NSLOCTEXT("UpgradeDescWidget", "LockedByParentStateDescText", "이전 노드 업그레이드 필요");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|State")
	FText DefaultStateDescText = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|State")
	int32 CostSwitcherIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|State")
	int32 StateDescSwitcherIndex = 1;

	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandlePopupClosed();

	void RefreshTextBlocks();
	void RefreshCostTextStyle();
	FText BuildCostNeedText() const;
	FText BuildCostText() const;
	bool ShouldShowStateDescText(const FText& StateText) const;
	void RefreshStateDescSwitcher(const FText& StateText);

	bool bClearWhenClosed = false;
};
