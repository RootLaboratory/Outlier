#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "StatAllocatorWidget.generated.h"

class APartnerCharacter;
class AOutlierPlayerState;
class AShooterCharacter;
class UButton;
class UImage;
class UCanvasPanel;
class UPanelWidget;
class UTextBlock;
class UUpgradeNodeGroupWidget;
class UWidgetSwitcher;

/** Blueprint supplies the layout only; stat allocator behavior lives in C++. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UStatAllocatorWidget : public UUserWidget,
	public IUILayerInputReceiver,
	public IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	void InjectCharacters(AShooterCharacter* InShooterCharacter, APartnerCharacter* InPartnerCharacter);

	AShooterCharacter* GetShooterCharacter() const { return ShooterCharacter; }

	APartnerCharacter* GetPartnerCharacter() const { return PartnerCharacter; }

	void ShowIntroPage();

	void ShowAllocatorPage();

	int32 GetCurrentPageIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RefreshUpgradeNodeGroup();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;

	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> StatAllocatorSwitcher;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ESCButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AbilityDescText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WaitingText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NodeCountText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> NodeImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeNodeGroupWidget> DefaultNodeGroupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeNodeGroupWidget> ShooterNodeGroupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TSubclassOf<UUpgradeNodeGroupWidget> PartnerNodeGroupWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(Transient)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UUpgradeNodeGroupWidget> ActiveNodeGroupWidget;

private:
	UFUNCTION()
	void HandleStartButtonClicked();

	UFUNCTION()
	void HandleESCButtonClicked();

	void BindOwningPlayerState();
	void UnbindPlayerState();
	void BindStatAllocatorExitPendingStates();
	void UnbindStatAllocatorExitPendingStates();
	void RefreshNodeCountText();
	void HandleNodeCountChanged(int32 NewNodeCount);
	void HandleStatAllocatorExitPendingChanged(AOutlierPlayerState* ChangedPlayerState);
	void RebuildUpgradeNodeGroup();
	UPanelWidget* GetUpgradeNodeGroupHost() const;
	TSubclassOf<UUpgradeNodeGroupWidget> ResolveNodeGroupWidgetClass() const;
	AOutlierPlayerState* GetLocalOutlierPlayerState() const;
	AOutlierPlayerState* FindPairedOutlierPlayerState() const;
	void ResetLocalStatAllocatorExitPending();
	void RequestStatAllocatorExit();
	void RefreshStatAllocatorExitPendingState();
	bool IsLocalStatAllocatorExitPending() const;
	bool IsPairStatAllocatorExitConfirmed() const;
	void TryPopStatAllocatorLayer();

	bool SetActivePage(int32 PageIndex);

	TWeakObjectPtr<AOutlierPlayerState> BoundPlayerState;
	TArray<TWeakObjectPtr<AOutlierPlayerState>> BoundExitPendingPlayerStates;
	FDelegateHandle NodeCountChangedHandle;
	bool bLocalStatAllocatorExitRequested = false;

	static constexpr int32 IntroPageIndex = 0;
	static constexpr int32 AllocatorPageIndex = 1;
};
