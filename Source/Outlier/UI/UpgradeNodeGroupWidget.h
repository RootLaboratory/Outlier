#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Styling/SlateBrush.h"
#include "OutlierPlayerState.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/UILayerContextReceiver.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UpgradeNodeGroupWidget.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UDataTable;
class UPanelWidget;
class UOutlierUpgradeComponent;
class UOutlierUpgradeSetData;
class UTextBlock;
class UTexture2D;
class UUpgradeDescWidget;
class UUpgradeNodeWidget;
class UWidget;
class UWidgetTree;
class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

USTRUCT(BlueprintType)
struct OUTLIER_API FUpgradeLineBinding
{
	GENERATED_BODY()

	// 활성 선을 표시할 Image 이름. 배치/크기는 UMG에서 설정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines", meta = (GetOptions = "GetLineImageOptions"))
	FName ImageName;
	// 같은 WBP 안에서 연결선의 시작·끝 노드를 선택한다.
	// 시작을 None으로 두면 이 선은 처음부터 활성 색상으로 완성해 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines", meta = (GetOptions = "GetLineNodeOptions"))
	FName SourceNodeWidgetName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines", meta = (GetOptions = "GetLineNodeOptions"))
	FName DestinationNodeWidgetName;
	// 비어 있으면 Image의 기존 텍스처를 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	TObjectPtr<UTexture2D> LineTexture;
	// 선을 따라 이동한 거리를 0~1로 담은 텍스처. 선 이미지와 같은 UV를 쓰며 sRGB를 끈다.
	// 지정하면 UseHorizontal/Reverse 대신 이 텍스처의 시작(0) -> 끝(1) 방향을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	TObjectPtr<UTexture2D> PathProgressTexture;
	// 0이면 직선 텍스처를 자동 측정한다. 곡선 등 수동 지정이 필요할 때만 길이를 입력한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "Upgrade|Lines", meta = (ClampMin = "0", DisplayName = "Line Length Override"))
	float LineLength = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	bool bUseHorizontal = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	bool bReverse = false;

	// 원본 알파에서 측정한 직선 양 끝의 UV 차이. 패키지에서도 사용할 수 있도록 저장한다.
	UPROPERTY()
	FVector2D MeasuredLineExtent = FVector2D::ZeroVector;
	UPROPERTY()
	TObjectPtr<UTexture2D> MeasuredTexture;
	UPROPERTY()
	FGuid MeasuredTextureSourceId;
};

USTRUCT()
struct OUTLIER_API FUpgradeLineRuntime
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UImage> Image;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	FSlateBrush OriginalBrush;
	float OriginalOpacity = 1.0f;
	ESlateVisibility OriginalVisibility = ESlateVisibility::Visible;
	FName ActivationNodeRowName;
	UPROPERTY(Transient)
	TObjectPtr<UUpgradeNodeWidget> SourceNode;
	float Length = 0.0f;
	FVector2D AutoLengthExtent = FVector2D::ZeroVector;
	float Progress = 0.0f;
	bool bSourceActivated = false;
	bool bAnimating = false;
	double FillStartedTime = 0.0;
};

UCLASS(Blueprintable, meta = (DisplayName = "Upgrade Node Group Widget"))
class OUTLIER_API UUpgradeNodeGroupWidget : public UUserWidget, public IUILayerInputReceiver,
	public IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual void InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors) override;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void InjectUpgradeContext(
		AShooterCharacter* InShooterCharacter,
		APartnerCharacter* InPartnerCharacter,
		UOutlierUpgradeComponent* InUpgradeComponent,
		AOutlierPlayerState* InPlayerState);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RefreshNodeWidgets();

	// 런타임에서 연결선 설정을 바꾼 경우 명시적으로 재구성한다. 기존 활성 선은 즉시 완성한다.
	UFUNCTION(BlueprintCallable, Category = "Upgrade|Lines")
	void RebuildConnectionLines();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	TObjectPtr<UMaterialInterface> LineFillMaterial;
	// 이 그룹의 모든 연결선에 같은 채움 색상을 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	FLinearColor LineFillColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines", meta = (ClampMin = "0"))
	float LineFillDuration = 0.35f;
	// UMG 단위/초. 0이면 기존 시간 방식, 양수이면 자동/수동 측정된 선에 길이/속도를 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines", meta = (ClampMin = "0"))
	float LineFillSpeed = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Lines")
	TArray<FUpgradeLineBinding> ConnectionLines;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	UOutlierUpgradeComponent* GetUpgradeComponent() const { return UpgradeComponent; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	EOutlierUpgradeRole GetUpgradeRole() const { return UpgradeRole; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	UDataTable* GetUpgradeDataTable() const { return UpgradeDataTable; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	UOutlierUpgradeSetData* GetUpgradeSetData() const { return UpgradeSetData; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	EOutlierUpgradeRole GetResolvedUpgradeRole() const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	UDataTable* GetResolvedUpgradeDataTable() const;

	bool IsNodeIdSelectedByOtherWidget(
		const UUpgradeNodeWidget* RequestingWidget,
		FName InTreeId,
		FName InNodeId) const;

	void DismissOtherNodeDescriptions(const UUpgradeNodeWidget* HoveredNode) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TObjectPtr<UOutlierUpgradeSetData> UpgradeSetData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	EOutlierUpgradeRole UpgradeRole = EOutlierUpgradeRole::Shooter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

	// 모든 Unlocked 노드가 공통으로 사용하는 상태 이미지다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Unlocked State Texture", meta = (DisplayName = "Unlocked Texture"))
	TObjectPtr<UTexture2D> UnlockedNodeTexture;

	// Parent가 아직 활성화되지 않은 Locked 노드가 공통으로 사용하는 이미지다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade|Deactivated State Texture", meta = (DisplayName = "Deactivated Texture"))
	TObjectPtr<UTexture2D> DeactivatedNodeTexture;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Upgrade")
	TObjectPtr<UPanelWidget> NodeWidgetHost;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Upgrade")
	TObjectPtr<UTextBlock> NodeCount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeDescWidget> UpgradeDescWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UOutlierUpgradeComponent> UpgradeComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<AOutlierPlayerState> PlayerState;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TArray<TObjectPtr<UUpgradeNodeWidget>> NodeWidgets;

private:
	friend struct FUpgradeConnectionLineTestAccess;
	UFUNCTION()
	TArray<FName> GetLineImageOptions() const;
	UFUNCTION()
	TArray<FName> GetLineNodeOptions() const;
	const UWidgetTree* GetLineWidgetTree() const;
	FName ResolveLineNodeRowName(FName WidgetName) const;
	float MeasureDisplayedLineLength(const FUpgradeLineRuntime& Line) const;
#if WITH_EDITOR
	void UpdateLineMeasurements();
#endif
	void RefreshConnectionLines(bool bImmediate);
	void HandleUpgradePresentationChanged();
	void HandleUnlockPresentationFinished(UUpgradeNodeWidget* Node);
	void HandleUnlockPresentationCancelled(UUpgradeNodeWidget* Node);
	void ResetNodeUnlockPresentations();
	void BindUpgradePresentationChanged();
	void UnbindUpgradePresentationChanged();
	bool IsLineSourceActivated(FName RowName) const;
	void SetLineProgress(FUpgradeLineRuntime& Line, float Progress);
	float GetLineFillDuration(const FUpgradeLineRuntime& Line) const;
	bool AdvanceLineFills(float DeltaSeconds, double Now = 0.0);
	bool TickLineFills(float DeltaSeconds);
	void StopLineFillTicker();
	void ClearConnectionLines();

	UPROPERTY(Transient)
	TArray<FUpgradeLineRuntime> RuntimeLines;
	TWeakObjectPtr<AOutlierPlayerState> BoundLinePlayerState;
	FDelegateHandle UpgradePresentationChangedHandle;
	FTSTicker::FDelegateHandle LineFillTickerHandle;
	uint32 CachedRestoreRevision = 0;
	double LastLineFillTime = 0.0;
	UFUNCTION()
	void HandleUpgradeStateChanged();

	void CacheNodeWidgets();
	void CacheNodeWidgetsRecursive(UWidget* Widget);
	bool IsNodeIdSelectedByOtherWidgetRecursive(
		const UWidget* Widget,
		const UUpgradeNodeWidget* RequestingWidget,
		FName InTreeId,
		FName InNodeId) const;
	bool DoesWidgetReferenceNode(
		const UUpgradeNodeWidget* NodeWidget,
		FName InTreeId,
		FName InNodeId) const;
	void BindUpgradeStateChanged();
	void UnbindUpgradeStateChanged();
	void BindPlayerStateNodeCountChanged();
	void UnbindPlayerStateNodeCountChanged();
	void HandleNodeCountChanged(int32 NewNodeCount);
	void RefreshNodeCountText();
	UTexture2D* ResolveNodeTexture(const UUpgradeNodeWidget* NodeWidget) const;
	UTexture2D* FindNodeTexture(FName NodeRowName) const;

	FDelegateHandle NodeCountChangedHandle;
};
