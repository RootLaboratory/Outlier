#pragma once

#include "CoreMinimal.h"
#include "UI/InfoQueueWidget.h"
#include "BottomTTSScriptWidget.generated.h"

class UTextBlock;
class FBottomTTSSpeakerLayout;

/** Resolves EventTag -> TTSTag -> Speaker/Script and stays visible until its voice finishes or is interrupted. */
UCLASS(Blueprintable)
class OUTLIER_API UBottomTTSScriptWidget : public UInfoQueueWidget
{
	GENERATED_BODY()

public:
	UBottomTTSScriptWidget(const FObjectInitializer& ObjectInitializer);

	/** Bind a TextBlock named Speaker in the WBP. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Speaker;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ScriptText;

	/** Keys match the DT's speaker strings exactly. Configure colors in the WBP. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Info|Speaker")
	TMap<FString, FLinearColor> SpeakerColors;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Info|Speaker")
	FLinearColor DefaultSpeakerColor = FLinearColor::White;

	/** Gap between the speaker's right edge and the first rendered subtitle line, in widget units. */
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
