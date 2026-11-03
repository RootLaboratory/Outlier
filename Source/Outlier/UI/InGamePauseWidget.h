#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerInputReceiver.h"
#include "InGamePauseWidget.generated.h"

class UBackgroundBlur;

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInGamePauseWidget : public UUserWidget,
	public IUILayerInputReceiver
{
	GENERATED_BODY()

protected:
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "InGame Pause")
	TObjectPtr<UBackgroundBlur> PauseBlurPanel;
};
