#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UpgradeDescWidget.generated.h"

class UTextBlock;
class UButton;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMediaTexture;
class UPopupRetainerBox;
class UTexture2D;

DECLARE_MULTICAST_DELEGATE(FOnUpgradeDescWidgetEvent);

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UUpgradeDescWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	FOnUpgradeDescWidgetEvent OnPurchaseRequested;

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

	FVector2D GetPopupDesignSize() const;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade")
	TObjectPtr<UPopupRetainerBox> PopupRetainer;

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Name;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Desc;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Cost;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> Button;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Background;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> MediaImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Media")
	TObjectPtr<UMediaTexture> MediaTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Media")
	TObjectPtr<UMaterialInterface> MediaMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Media")
	FName MediaTextureParameterName = TEXT("Texture");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Background")
	TObjectPtr<UTexture2D> DefaultBackgroundTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Background")
	TObjectPtr<UTexture2D> PurchasedBackgroundTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Button")
	TObjectPtr<UTexture2D> InsufficientDisabledTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Button")
	TObjectPtr<UTexture2D> PrerequisiteDisabledTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Button")
	TObjectPtr<UTexture2D> PurchasedDisabledTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FSlateColor DefaultCostTextColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FSlateColor InsufficientCostTextColor = FSlateColor(FLinearColor(1.0f, 0.1f, 0.1f, 1.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Cost")
	FText CostNeedTextFormat = NSLOCTEXT("UpgradeDescWidget", "CostNeedTextFormat", "{0}");

	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandlePopupClosed();

	UFUNCTION()
	void HandlePurchaseClicked();

	void RefreshTextBlocks();
	void InitializeMediaImage();
	void RefreshBackground();
	void RefreshCostTextStyle();
	void RefreshButtonStyle();
	FText BuildCostNeedText() const;

	UPROPERTY(Transient)
	FSlateBrush DefaultDisabledButtonBrush;

	UPROPERTY(Transient)
	FSlateBrush DefaultBackgroundBrush;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MediaImageMaterial;

	bool bDefaultDisabledButtonBrushCached = false;
	bool bDefaultBackgroundBrushCached = false;
	bool bClearWhenClosed = false;
	FVector2D DefaultPopupDesignSize = FVector2D::ZeroVector;
};
