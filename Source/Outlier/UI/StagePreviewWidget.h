#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StagePreviewWidget.generated.h"

class UImage;
class UTexture2D;

/** Stage preview image and its decorative frame share one visibility state. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UStagePreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool SetPreviewTexture(UTexture2D* Texture);
	void SetPreviewVisible(bool bVisible);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PreviewImage;
};
