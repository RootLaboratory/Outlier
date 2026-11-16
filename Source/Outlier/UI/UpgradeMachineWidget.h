#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UpgradeMachineWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUpgradeWaitingStateChanged);

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

	UFUNCTION(BlueprintCallable, Category = "Upgrade|Waiting")
	void CancelExit();

	UFUNCTION(BlueprintPure, Category = "Upgrade|Waiting")
	bool IsWaitingForPartner() const { return bLocalExitRequested; }

	UFUNCTION(BlueprintPure, Category = "Upgrade|Waiting")
	bool CanCancelExit() const { return bSessionSynchronized && bLocalExitRequested && !bExitRequestInFlight && !bExitFinalized; }

	UPROPERTY(BlueprintAssignable, Category = "Upgrade|Waiting")
	FOnUpgradeWaitingStateChanged OnWaitingStateChanged;

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
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FUpgradeMachineTestAccessor;
#endif
	UFUNCTION()
	void HandleButtonClicked();

	void BindSessionState();
	void UnbindSessionState();
	void HandleSessionChanged(uint32 SessionId, uint32 Generation, bool bPending, bool bFinalized, uint32 SyncRequestId);
	void RefreshWaitingState();
	void AdvanceWaitingMessage();
	void UpdateWaitingMessage();
	void SynchronizeSession();
	void OpenNodeScreen();
	void RequestExit();
	void TryPopMachineLayer();
	AOutlierPlayerState* GetLocalPlayerState() const;
	TSubclassOf<UUpgradeNodeGroupWidget> ResolveNodeGroupClass() const;

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(Transient)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UUpgradeNodeGroupWidget> ActiveNodeGroupWidget;

	TWeakObjectPtr<AOutlierPlayerState> BoundSessionPlayerState;
	TWeakObjectPtr<AOutlierPlayerState> OpenedPlayerState;
	FTimerHandle WaitingMessageTimerHandle;
	FTimerHandle FinalizedCloseRetryHandle;
	uint32 OpenedGameplayGeneration = 0;
	int32 WaitingMessageIndex = 0;
	uint32 OpenedSessionId = 0;
	uint32 SessionSyncRequestId = 0;
	bool bSessionSynchronized = false;
	bool bExitRequestInFlight = false;
	bool bExitFinalized = false;
	bool bFinalizedCloseRetryUsed = false;
	bool bLocalExitRequested = false;
	bool bUIOpenedReported = false;
};
