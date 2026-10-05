#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/UILayerTypes.h"
#include "TitleWidget.generated.h"

class UCreditWidget;
class ULobbyWidget;
class USettingWidget;
class UTitleButtonsWidget;
class UUILayerKeyHintWidget;
enum class ETitleButtonAction : uint8;

UCLASS()
class OUTLIER_API UTitleWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	// 버튼과 그 입력은 모두 이 모듈이 판단하고, 이 레이어는 확정된 액션으로 다음 레이어를 연다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTitleButtonsWidget> TitleButtons;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Layer")
	TSubclassOf<ULobbyWidget> LobbyWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Layer")
	TSubclassOf<UCreditWidget> CreditWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Layer")
	TSubclassOf<USettingWidget> SettingWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Layer")
	TSubclassOf<UUILayerKeyHintWidget> KeyHintWidgetClass;

private:
	void HandleTitleButtonAction(ETitleButtonAction Action);
	void PushLobbyLayer();
	void PushCreditLayer();
	void PushSettingLayer();
	void PushKeyHintLayer();
	void RequestExit();

	UFUNCTION()
	void HandleLobbyBackRequested();

	UPROPERTY(Transient)
	TObjectPtr<ULobbyWidget> ActiveLobbyWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUILayerKeyHintWidget> ActiveKeyHintWidget;

	FUILayerHandle LobbyLayerHandle;
	FUILayerHandle KeyHintLayerHandle;
};
