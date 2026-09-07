#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OutlierPlayerState.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "UpgradeNodeGroupWidget.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UDataTable;
class UPanelWidget;
class UOutlierUpgradeComponent;
class UOutlierUpgradeSetData;
class UTexture2D;
class UUpgradeDescWidget;
class UUpgradeNodeWidget;
class UWidget;

UCLASS(Blueprintable, meta = (DisplayName = "Upgrade Node Group Widget"))
class OUTLIER_API UUpgradeNodeGroupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void InjectUpgradeContext(
		AShooterCharacter* InShooterCharacter,
		APartnerCharacter* InPartnerCharacter,
		UOutlierUpgradeComponent* InUpgradeComponent,
		AOutlierPlayerState* InPlayerState);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RefreshNodeWidgets();

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
	UTexture2D* ResolveNodeTexture(const UUpgradeNodeWidget* NodeWidget) const;
	UTexture2D* FindNodeTexture(FName NodeRowName) const;

	FDelegateHandle NodeCountChangedHandle;
};
