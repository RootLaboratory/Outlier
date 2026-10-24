#pragma once

#include "CoreMinimal.h"
#include "Event/OutlierEventTypes.h"
#include "UI/InfoQueueWidget.h"
#include "RightGifBindedWidget.generated.h"

class UGifSubjectWidget;
class UGifInputWidget;
class UImage;
class UMediaPlayer;
class UMediaTexture;
class UTextBlock;

/** 오른쪽 영역 Queue, GIF 메타데이터, MP4 재생, 선택적 입력 키를 소유한다. */
UCLASS(Blueprintable)
class OUTLIER_API URightGifBindedWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	/** Subject 레이아웃과 내부 아이콘/텍스트 바인딩은 이 모듈의 WBP가 담당한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGifSubjectWidget> SubjectWidget;

	/** Input 레이아웃과 내부 키/설명 바인딩은 이 모듈의 WBP가 담당한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGifInputWidget> InputWidget;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DescriptionText;

	/** MP4/Poster 표시 영역. 크기와 종횡비는 WBP의 SizeBox가 담당한다. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> GifImage;

protected:
	virtual void StartInfoPresentation() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const override;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) override;
	virtual int32 GetPresentationWarmupFrames() const override { return 2; }
	virtual void ResetInfoContent() override;
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) override;

private:
	bool TryResolveGifData(const FOutlierInfoWidgetRequest& Request, FOutlierGifDataRow& OutData) const;
	void StartGifMedia(const FOutlierInfoWidgetRequest& Request);
	void StopGifMedia();

	UFUNCTION()
	void HandleGifMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleGifMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void HandleGifMediaEndReached();

	UPROPERTY(Transient)
	FOutlierGifDataRow CurrentGifData;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> GifPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> GifTexture;

	/** 영상 종료 시 완료할 Source 요청. 미디어 콜백 안에서 큐를 진행하지 않도록 Tick에서 완료한다. */
	FGameplayTag MediaEventTag;
	float MediaSafetyRemaining = 0.0f;
	int32 MediaPlayedCount = 0;
	bool bMediaOpening = false;
	bool bWaitMediaEnd = false;
	bool bMediaEndPending = false;
	bool bGifPausedWithWorld = false;
};
