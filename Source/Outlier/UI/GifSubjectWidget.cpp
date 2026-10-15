#include "UI/GifSubjectWidget.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UGifSubjectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ClearSubjectData();
}

void UGifSubjectWidget::SetSubjectData(const FText& Text, bool bShowIcon, UTexture2D* IconTexture)
{
	ClearSubjectData();
	if (SubjectText) SubjectText->SetText(Text);
	const bool bHasIcon = bShowIcon && IsValid(IconTexture) && SubjectIcon && SubjectIconContainer;
	if (bHasIcon)
	{
		// ScaleBox는 원본 종횡비를 사용하고, 표시 영역은 SizeBox가 제어한다.
		SubjectIcon->SetBrushFromTexture(IconTexture, true);
		SubjectIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		SubjectIconContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	SetVisibility(!Text.IsEmpty() || bHasIcon ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UGifSubjectWidget::ClearSubjectData()
{
	if (SubjectText) SubjectText->SetText(FText::GetEmpty());
	if (SubjectIconContainer) SubjectIconContainer->SetVisibility(ESlateVisibility::Collapsed);
	if (SubjectIcon)
	{
		SubjectIcon->SetBrushFromTexture(nullptr, false);
		SubjectIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}
