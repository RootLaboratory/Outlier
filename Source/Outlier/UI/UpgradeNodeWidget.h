#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UObject/PropertyText.h"
#include "UpgradeNodeWidget.generated.h"

class UButton;
class UCanvasPanel;
class UDataTable;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UOutlierUpgradeComponent;
class UOutlierUpgradeSetData;
class UTexture2D;
class UUpgradeDescWidget;
class UUpgradeNodeGroupWidget;
class AOutlierPlayerState;
class AFirstPersonPlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUpgradeNodeWidgetEvent, UUpgradeNodeWidget*, NodeWidget, FName, NodeRowName);

UCLASS(Blueprintable, meta = (DisplayName = "Upgrade Node Widget"))
class OUTLIER_API UUpgradeNodeWidget : public UUserWidget
{
	GENERATED_BODY()
	friend struct FUpgradeNodeInteractionTestAccess;

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

	void DismissDescription();

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

	UFUNCTION(BlueprintPure, Category = "Upgrade|Hold")
	float GetHoldProgress() const { return HoldProgress; }

	UFUNCTION(BlueprintPure, Category = "Upgrade|Hold")
	bool IsHoldingUpgrade() const { return bHoldingUpgrade; }

	UFUNCTION(BlueprintPure, Category = "Upgrade|Hold")
	bool IsUpgradeRequestPending() const { return PendingRequestId != 0; }

	// 화면을 닫거나 컨텍스트를 바꿀 때 로컬 입력과 응답 대기를 정리한다. 이미 보낸 구매는 취소하지 않는다.
	UFUNCTION(BlueprintCallable, Category = "Upgrade|Hold")
	void ResetUpgradeInteraction();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|Hold", meta = (ClampMin = "0.01", Units = "s"))
	float HoldDuration = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade|Hold", meta = (ClampMin = "0.01", Units = "s"))
	float RequestTimeout = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetNodeRowNameOptions"))
	FName NodeRowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetTreeIdOptions"))
	FName TreeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (GetOptions = "GetNodeIdOptions"))
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	bool bDisableWhenLocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	TSubclassOf<UUpgradeDescWidget> UpgradeDescWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Desc")
	FVector2D DescPopupOffset = FVector2D(24.0f, 0.0f);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance")
	TObjectPtr<UMaterialInterface> NodeColorMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance")
	bool bUseMaterialStateColors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance")
	TObjectPtr<UTexture2D> NodeIconTextureOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance")
	TObjectPtr<UTexture2D> LockedIconTextureOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance")
	TObjectPtr<UTexture2D> UnlockedIconTextureOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade|Appearance", meta = (DisplayName = "Show As Already Activated", ToolTip = "Shows the unlocked icon and purchased description. Visual only; does not activate the upgrade in gameplay."))
	bool bAlwaysShowUnlockedIcon = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Appearance")
	FLinearColor LockedColor = FLinearColor(0.35f, 0.42f, 0.48f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Appearance")
	FLinearColor UnlockedColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Appearance")
	FLinearColor ActivatedColor = FLinearColor(0.3f, 0.85f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OverlayIntensity = 1.0f;

	// 기존 BP 참조는 유지하되 클릭 구매를 우회하지 않도록 더 이상 방송하지 않는다.
	UPROPERTY(BlueprintAssignable, Category = "Upgrade", meta = (DeprecatedProperty, DeprecationMessage = "Upgrade purchases now use hold input."))
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeClicked;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeHovered;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnUpgradeNodeWidgetEvent OnUpgradeNodeUnhovered;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade")
	TObjectPtr<UButton> NodeButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Upgrade|Appearance")
	TObjectPtr<UImage> Background;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Upgrade|Appearance")
	TObjectPtr<UImage> Frame;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Upgrade|Appearance")
	TObjectPtr<UImage> Glow;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade|Appearance")
	TObjectPtr<UImage> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FName CurrentNodeRowName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	FOutlierUpgradeNodeRow CurrentNodeData;

	UPROPERTY(BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeNodeState CurrentState = EOutlierUpgradeNodeState::Unlocked;

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
	void HandlePressed();

	UFUNCTION()
	void HandleReleased();

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
	virtual void BeginDestroy() override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	UFUNCTION()
	void HandleUpgradeStateChanged();
	void HandleNodeCountChanged(int32 NewNodeCount);
	void HandleApplicationActivationChanged(bool bIsActive);
	void BindUpgradeContextEvents();
	void UnbindUpgradeContextEvents();
	bool CanBeginHold() const;
	bool AdvanceHold(float DeltaSeconds);
	void CancelHold();
	void RequestUpgrade();
	void HandleUpgradeResult(uint32 RequestId, AActor* UpgradeOwner, FName RowName, bool bSucceeded);
	void CacheInteractionVisibility();
	bool IsInteractionVisible() const;
	void StartInteractionTicker();
	void StopInteractionTicker();
	bool TickUpgradeInteraction(float DeltaSeconds);
	void CheckRequestTimeout(double Now);
	void ClearPendingRequest();

	void BindButtonEvents();
	UOutlierUpgradeComponent* ResolveUpgradeComponent();
	FName GetNodeLookupName() const;
	bool TryResolveNodeRowNameByTreeAndNodeId(FName InTreeId, FName InNodeId, FName& OutRowName) const;
	const UDataTable* FindOwningUpgradeDataTable() const;
	const UUpgradeNodeGroupWidget* FindOwningNodeGroupWidget() const;
	bool ShouldShowDescOnHover() const;
	EOutlierUpgradeNodeState GetDisplayedDescState() const;
	UUpgradeDescWidget* EnsureUpgradeDescWidget();
	void HideUpgradeDescWidget();
	void ScheduleDescHide();
	void HandleDescHideTimer();
	UCanvasPanel* FindDescCanvas() const;
	bool CalculateDescWidgetLayout(UCanvasPanel* ParentCanvas, FVector2D& OutPosition, FVector2D& OutSize, float& OutRenderScale) const;
	float CalculateDescViewportScale(const FVector2D& CanvasSize) const;
	static FText BuildNodeRowOptionDisplayName(FName RowName, const FOutlierUpgradeNodeRow& NodeRow);
	void ApplyInjectedPlayerState(FOutlierUpgradeNodeViewData& InOutViewData) const;
	void RefreshVisibleDescWidget();
	void RefreshEnabledState();
	UMaterialInstanceDynamic* EnsureOverlayMaterial(UImage* Image, UMaterialInstanceDynamic* CachedMaterial) const;
	void RefreshNodeAppearance();
	void RefreshNodeTexture();

	bool bHoverDescVisible = false;
	bool bSuppressDescUntilUnhover = false;
	bool bHoldingUpgrade = false;
	float HoldProgress = 0.0f;
	double LastInteractionTime = 0.0;
	double RequestStartedTime = 0.0;
	uint32 PendingRequestId = 0;
	FName PendingNodeRowName = NAME_None;
	TWeakObjectPtr<AActor> PendingUpgradeOwner;
	TWeakObjectPtr<AFirstPersonPlayerController> UpgradeController;
	TWeakObjectPtr<UOutlierUpgradeSetData> CachedUpgradeSetData;
	TArray<TWeakObjectPtr<UWidget>> InteractionVisibilityWidgets;
	FTSTicker::FDelegateHandle InteractionTickerHandle;
	FDelegateHandle ApplicationActivationHandle;
	FDelegateHandle UpgradeResultHandle;
	FDelegateHandle NodeCountChangedHandle;
	FDelegateHandle ActivatedNodesChangedHandle;
	FTimerHandle DescHideTimerHandle;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrameMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> IconMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> UnlockedNodeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DeactivatedNodeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NodeTexture;
};
