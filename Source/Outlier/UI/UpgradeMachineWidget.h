#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UpgradeMachineWidget.generated.h"

class AOutlierPlayerState;
class APartnerCharacter;
class AShooterCharacter;
class UButton;
class UImage;
class UTextBlock;
class UUpgradeNodeGroupWidget;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UUpgradeMachineWidget : public UUserWidget,
	public IUILayerInputReceiver,
	public IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	UUpgradeMachineWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextBlock_76;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Ex;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Background;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Nodes")
	TSubclassOf<UUpgradeNodeGroupWidget> DefaultNodeGroupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Nodes")
	TSubclassOf<UUpgradeNodeGroupWidget> ShooterNodeGroupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Nodes")
	TSubclassOf<UUpgradeNodeGroupWidget> PartnerNodeGroupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Waiting", meta = (EditFixedSize))
	TArray<FText> WaitingMessages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Upgrade|Waiting", meta = (ClampMin = "0.1"))
	float WaitingMessageInterval = 0.6f;

private:
	UFUNCTION()
	void HandleButtonClicked();

	void BindExitPendingStates();
	void UnbindExitPendingStates();
	void HandleExitPendingChanged(AOutlierPlayerState* ChangedPlayerState);
	void RefreshWaitingState();
	void AdvanceWaitingMessage();
	void OpenNodeScreen();
	void RequestExit();
	void TryPopMachineLayer();
	AOutlierPlayerState* GetLocalPlayerState() const;
	AOutlierPlayerState* FindPairedPlayerState() const;
	TSubclassOf<UUpgradeNodeGroupWidget> ResolveNodeGroupClass() const;

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(Transient)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UUpgradeNodeGroupWidget> ActiveNodeGroupWidget;

	TArray<TWeakObjectPtr<AOutlierPlayerState>> BoundExitPendingPlayerStates;
	TWeakObjectPtr<AOutlierPlayerState> OpenedPlayerState;
	FTimerHandle WaitingMessageTimerHandle;
	uint32 OpenedGameplayGeneration = 0;
	int32 WaitingMessageIndex = 0;
	bool bLocalExitRequested = false;
	bool bUIOpenedReported = false;
};
