#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GifSubjectWidget.generated.h"

class UHorizontalBox;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;

/** Subject 레이아웃 모듈. 아이콘 표시 영역, 스케일링, 텍스트 스타일은 WBP가 담당한다. */
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

	/** ScaleBox -> SubjectIcon을 포함한다. 아이콘이 없으면 통째로 Collapsed된다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SubjectIconContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SubjectIcon;

protected:
	virtual void NativeOnInitialized() override;

};
