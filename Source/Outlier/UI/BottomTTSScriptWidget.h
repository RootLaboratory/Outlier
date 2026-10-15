#pragma once

#include "CoreMinimal.h"
#include "UI/InfoQueueWidget.h"
#include "BottomTTSScriptWidget.generated.h"

class UTextBlock;
class FBottomTTSSpeakerLayout;

/** EventTag -> TTSTag -> Speaker/Script를 해석하고, 음성이 끝나거나 중단될 때까지 표시를 유지한다. */
UCLASS(Blueprintable)
class OUTLIER_API UBottomTTSScriptWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	UBottomTTSScriptWidget(const FObjectInitializer& ObjectInitializer);

	/** WBP에서 Speaker라는 이름의 TextBlock을 바인딩한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Speaker;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ScriptText;

	/** 키는 DT의 speaker 문자열과 정확히 일치해야 한다. 색상은 WBP에서 설정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Info|Speaker")
	TMap<FString, FLinearColor> SpeakerColors;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Info|Speaker")
	FLinearColor DefaultSpeakerColor = FLinearColor::White;

	/** speaker 오른쪽 끝과 첫 번째 자막 줄 사이 간격(widget 단위). */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Info|Speaker", meta = (ClampMin = "0.0"))
	float SpeakerGap = 12.0f;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual bool PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const override;
	virtual bool PresentInfo(const FOutlierInfoWidgetRequest& Request) override;
	virtual void ResetInfoContent() override;
	virtual void InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted) override;

private:
	void UpdateSpeakerPosition(const TSharedRef<SWidget>& Content, const FGeometry& ContentGeometry);
	TSharedPtr<FBottomTTSSpeakerLayout> SpeakerLayout;
	FVector2D OriginalSpeakerTranslation = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FString CurrentSpeaker;

	UPROPERTY(Transient)
	FText CurrentScript;
};
