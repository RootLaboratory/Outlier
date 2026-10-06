#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GifSubjectWidget.generated.h"

class UHorizontalBox;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;

/** Subject layout module. Its WBP owns the icon display area, scaling and text style. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGifSubjectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSubjectData(const FText& Text, bool bShowIcon, UTexture2D* IconTexture);
	void ClearSubjectData();

	/** HorizontalBox with Auto child slots for SubjectIconContainer and SubjectText. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> SubjectRow;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SubjectText;

	/** Contains ScaleBox -> SubjectIcon. Collapsed as a unit when no icon is displayed. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SubjectIconContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SubjectIcon;

protected:
	virtual void NativeOnInitialized() override;

};
