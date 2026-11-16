#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/DeveloperConvenienceTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/DeveloperConvenienceActionWidget.h"
#include "DeveloperConvenienceWidget.generated.h"

class UButton;
class UTextBlock;
class UPreSetLoadWidget;
class AFirstPersonPlayerController;
class UWidgetAnimation;

// 메뉴와 상대 대기 WBP가 공통으로 상속한다. 조작 가능 여부는 서버 Snapshot으로 결정한다.
UCLASS(Blueprintable)
class OUTLIER_API UDeveloperConvenienceWidget : public UUserWidget, public IUILayerInputReceiver
{
	GENERATED_BODY()
public:
	void InitializeDeveloperConvenience(const FDeveloperConvenienceSnapshot& Snapshot);
	void ReleaseDeveloperConvenienceContext();
	// 닫기 연출이 끝날 때까지 서버 세션과 양쪽 일시정지를 유지한다.
	UFUNCTION(BlueprintCallable, Category = "Developer Convenience")
	void RequestAnimatedClose();
	UFUNCTION(BlueprintPure, Category = "Developer Convenience")
	const FDeveloperConvenienceSnapshot& GetDeveloperConvenienceContext() const { return Context; }
	UFUNCTION(BlueprintPure, Category = "Developer Convenience")
	EDeveloperConveniencePage GetDeveloperConveniencePage() const { return Page; }
	UFUNCTION(BlueprintCallable, Category = "Developer Convenience")
	bool SetDeveloperConveniencePage(EDeveloperConveniencePage NewPage);
	UPROPERTY(BlueprintAssignable, Category = "Developer Convenience")
	FOnDeveloperConveniencePageChanged OnDeveloperConveniencePageChanged;
	UFUNCTION(BlueprintCallable, Category = "Developer Convenience")
	void ExecuteDeveloperConvenienceAction(EDeveloperConvenienceAction Action);
	UFUNCTION(BlueprintCallable, Category = "Developer Convenience")
	void ConfirmSelectedStage();
	UFUNCTION(BlueprintPure, Category = "Developer Convenience")
	FName GetPendingStageId() const { return PendingStageId; }

protected:
	// 시작은 화면 왼쪽 밖, 끝은 메뉴의 정상 위치로 제작한다. 닫기는 같은 연출을 역재생한다.
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> MenuSlide;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeDestruct() override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override { return true; }
	virtual bool HandleUILayerRight_Implementation() override { return true; }
	// 대기 화면은 같은 부모를 사용하므로 조작용 바인딩은 선택 사항이다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDeveloperConvenienceActionWidget> TeamRecoveryButton;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDeveloperConvenienceActionWidget> LevelSelectionButton;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDeveloperConvenienceActionWidget> ReturnToTitleButton;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LevelSelectionPanel;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPreSetLoadWidget> PresetLoad;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConfirmMoveButton;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedStageText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FeedbackText;
	// 기존 모듈의 StageId는 유지하고 표시 이름만 WBP 기본값에서 바꿀 수 있다.
	UPROPERTY(EditDefaultsOnly, Category = "Developer Convenience")
	TMap<FName, FText> StageDisplayNames;

private:
	friend class FDeveloperConvenienceMenuTest;
	bool CanOperate() const;
	void RefreshMenu();
	void SetFeedback(const FText& Message);
	FText GetStageName(FName StageId) const;
	bool IsCurrentResult(const FGuid& MatchId, const FGuid& SessionId) const;
	bool FinishActionResult(const FGuid& RequestId, const FGuid& MatchId, const FGuid& SessionId,
		int64 Generation, EDeveloperConvenienceAction Action);
	FText GetFailureText(EDeveloperConvenienceRequestResult Result) const;
	UFUNCTION()
	void HandleStageSelected(FName StageId);
	UFUNCTION()
	void HandleOptionResult(const FDeveloperConvenienceOptionResult& Result);
	UFUNCTION()
	void HandleRecoveryResult(const FDeveloperConvenienceRecoveryResult& Result);
	UFUNCTION()
	void HandlePresetResult(const FDeveloperConveniencePresetResult& Result);
	UFUNCTION()
	void HandleTitleResult(const FDeveloperConvenienceTitleResult& Result);
	UFUNCTION()
	void HandleMenuSlideFinished();
	UFUNCTION()
	void HandleSessionResult(const FDeveloperConvenienceSessionResult& Result);
	void SendCloseRequest();
	TWeakObjectPtr<AFirstPersonPlayerController> ObservedController;
	FName PendingStageId;
	FGuid PendingRequestId;
	EDeveloperConvenienceAction PendingAction = EDeveloperConvenienceAction::TeamRecovery;
	bool bActionPending = false;
	bool bExitAccepted = false;
	bool bClosing = false;
	bool bCloseRequestSent = false;
	FGuid CloseRequestId;
	UPROPERTY(Transient)
	FDeveloperConvenienceSnapshot Context;
	EDeveloperConveniencePage Page = EDeveloperConveniencePage::MainMenu;
	bool bContextActive = false;
};
