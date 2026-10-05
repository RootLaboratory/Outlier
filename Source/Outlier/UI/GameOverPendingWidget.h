#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/GameOverPendingTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "GameOverPendingWidget.generated.h"

class UTextBlock;
class UWidgetSwitcher;

/** 페어 제안 대기 화면. Switcher 0은 요청자, 1은 응답자다. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UGameOverPendingWidget : public UUserWidget, public IUILayerInputReceiver
{
	GENERATED_BODY()

public:
	UGameOverPendingWidget(const FObjectInitializer& ObjectInitializer);
	void InitializePendingRequest(const FGameOverPendingRequest& InRequest, bool bInRequester);

protected:
	virtual void NativeConstruct() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerEscape_Implementation() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Game Over|Pending")
	TObjectPtr<UWidgetSwitcher> PendingSwitcher;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Game Over|Pending")
	TObjectPtr<UTextBlock> ReceiverChoiceText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Game Over|Pending")
	TObjectPtr<UTextBlock> ReceiverLevelText;

	// Choice별 문구는 WBP Class Defaults에서 덮어쓸 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Game Over|Pending|Text")
	TMap<EGameOverPendingChoice, FText> ReceiverChoiceTexts;

	// LevelIndex(1~4)별 이름을 WBP Class Defaults에서 지정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Game Over|Pending|Text")
	TMap<int32, FText> ReceiverLevelTexts;

	UPROPERTY(BlueprintReadOnly, Category = "Game Over|Pending")
	FGameOverPendingRequest PendingRequest;

	UPROPERTY(BlueprintReadOnly, Category = "Game Over|Pending")
	bool bIsRequester = false;

private:
	void RefreshView();
	bool SubmitResponse(bool bApprove);

	bool bResponseSubmitted = false;
};
