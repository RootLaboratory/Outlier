#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "InGamePauseWidget.generated.h"

class UBackgroundBlur;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInGamePauseWidget : public UUserWidget,
	public IUILayerContextReceiver,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Pause")
	TObjectPtr<UBackgroundBlur> PauseBlurPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Pause")
	TObjectPtr<UTextBlock> PauseText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "InGame Pause|Text")
	FText ShooterPausedText = FText::FromString(TEXT("Shooter가 일시정지를 눌렀습니다"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "InGame Pause|Text")
	FText PartnerPausedText = FText::FromString(TEXT("Partner가 일시정지를 눌렀습니다"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "InGame Pause|Text")
	FText UnknownPausedText = FText::FromString(TEXT("상대가 일시정지를 눌렀습니다"));

private:
	void SetPauseTextFromPauser(AActor* PauserActor);
	void SetPauseText(const FText& NewPauseText);

	FText CurrentPauseText;
};
