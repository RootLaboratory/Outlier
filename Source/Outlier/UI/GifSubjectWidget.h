#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GifSubjectWidget.generated.h"

class UHorizontalBox;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;

/** Subject 레이아웃 모듈. WBP의 최대 아이콘 크기에 맞춰 이미지의 Desired Size를 계산한다. */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UGifSubjectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSubjectData(const FText& Text, bool bShowIcon, UTexture2D* IconTexture);
	void ClearSubjectData();

	/** SubjectIconContainer와 SubjectText를 Auto child slot으로 담는 HorizontalBox. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> SubjectRow;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SubjectText;

	/** SubjectIcon을 포함하며 최대 아이콘 크기를 지정한다. 아이콘이 없으면 통째로 Collapsed된다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SubjectIconContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SubjectIcon;

protected:
	virtual void NativeOnInitialized() override;

};
