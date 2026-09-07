#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UObject/PropertyText.h"
#include "UpgradeNodeWidget.generated.h"

class UButton;
class UCanvasPanel;
class UDataTable;
class UImage;
class UOutlierUpgradeComponent;
class UTexture2D;
class UUpgradeDescWidget;
class UUpgradeNodeGroupWidget;
class AOutlierPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUpgradeNodeWidgetEvent, UUpgradeNodeWidget*, NodeWidget, FName, NodeRowName);

UCLASS(Blueprintable, meta = (DisplayName = "Upgrade Node Widget"))
class OUTLIER_API UUpgradeNodeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void SynchronizeProperties() override;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void InjectNodeData(FName InNodeRowName, const FOutlierUpgradeNodeRow& InNodeData);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void InjectViewData(const FOutlierUpgradeNodeViewData& InViewData);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RefreshFromUpgradeComponent(UOutlierUpgradeComponent* UpgradeComponent);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeComponent(UOutlierUpgradeComponent* InUpgradeComponent);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradePlayerState(AOutlierPlayerState* InPlayerState);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	bool RefreshNodeRowNameFromIds();

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeDescWidget(UUpgradeDescWidget* InUpgradeDescWidget);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeDescWidgetClass(TSubclassOf<UUpgradeDescWidget> InUpgradeDescWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetNodeState(EOutlierUpgradeNodeState InState, bool bInCanAfford);

	UFUNCTION(BlueprintCallable, Category = "Upgrade|Texture")
	void SetUnlockedNodeTexture(UTexture2D* InUnlockedNodeTexture);

	UFUNCTION(BlueprintCallable, Category = "Upgrade|Texture")
	void SetDeactivatedNodeTexture(UTexture2D* InDeactivatedNodeTexture);

	UFUNCTION(BlueprintCallable, Category = "Upgrade|Texture")
	void SetNodeTexture(UTexture2D* InNodeTexture);

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool GetNodeData(FOutlierUpgradeNodeRow& OutNodeData) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	FName GetNodeRowName() const { return CurrentNodeRowName; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	EOutlierUpgradeNodeState GetNodeState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool CanAfford() const { return bCanAfford; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetNodeRowNameOptions"))
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetTreeIdOptions"))
	FName TreeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetNodeIdOptions"))
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	bool bDisableWhenLocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	bool bActivateNodeOnClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	TSubclassOf<UUpgradeDescWidget> UpgradeDescWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	FVector2D DescPopupOffset = FVector2D(24.0f, 12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	FVector2D DescViewportPadding = FVector2D(16.0f, 16.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	FVector2D DescFallbackSize = FVector2D(360.0f, 160.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	FVector2D DescBaseDesignResolution = FVector2D(1920.0f, 1080.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc", meta = (ClampMin = "0.01"))
	float DescMinViewportScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc", meta = (ClampMin = "0.01"))
	float DescMaxViewportScale = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	int32 DescPopupZOrder = 50;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeClicked;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeHovered;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeUnhovered;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade")
	TObjectPtr<UButton> NodeButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Upgrade|Texture")
	TObjectPtr<UImage> NodeImage;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FName CurrentNodeRowName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FOutlierUpgradeNodeRow CurrentNodeData;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeNodeState CurrentState = EOutlierUpgradeNodeState::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	bool bCanAfford = false;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	int32 CurrentNodeCount = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UOutlierUpgradeComponent> CachedUpgradeComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<AOutlierPlayerState> CachedPlayerState;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UUpgradeDescWidget> CachedUpgradeDescWidget;

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();

	UFUNCTION()
	TArray<FPropertyTextFName> GetNodeRowNameOptions() const;

	UFUNCTION()
	TArray<FPropertyTextFName> GetTreeIdOptions() const;

	UFUNCTION()
	TArray<FPropertyTextFName> GetNodeIdOptions() const;

private:
	virtual void NativeDestruct() override;

	void BindButtonEvents();
	UOutlierUpgradeComponent* ResolveUpgradeComponent();
	FName GetNodeLookupName() const;
	bool TryResolveNodeRowNameByTreeAndNodeId(FName InTreeId, FName InNodeId, FName& OutRowName) const;
	const UDataTable* FindOwningUpgradeDataTable() const;
	const UUpgradeNodeGroupWidget* FindOwningNodeGroupWidget() const;
	bool ShouldShowDescOnHover() const;
	UUpgradeDescWidget* EnsureUpgradeDescWidget();
	void HideUpgradeDescWidget();
	UCanvasPanel* FindDescCanvas() const;
	bool CalculateDescWidgetLayout(UCanvasPanel* ParentCanvas, FVector2D& OutPosition, FVector2D& OutSize, float& OutRenderScale) const;
	float CalculateDescViewportScale(const FVector2D& CanvasSize) const;
	static FText BuildNodeRowOptionDisplayName(FName RowName, const FOutlierUpgradeNodeRow& NodeRow);
	void ApplyInjectedPlayerState(FOutlierUpgradeNodeViewData& InOutViewData) const;
	void RefreshVisibleDescWidget();
	void RefreshEnabledState();
	void CacheDefaultNodeBrush();
	void RefreshNodeTexture();

	bool bHoverDescVisible = false;
	bool bDefaultNodeBrushCached = false;
	FSlateBrush DefaultNodeBrush;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> UnlockedNodeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DeactivatedNodeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NodeTexture;
};
