#include "UI/GifSubjectWidget.h"

#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UGifSubjectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 부모는 SubjectWidget의 고정 폭을 중앙에 배치하므로 내부 행도 중앙에 맞춘다.
	if (SubjectRow)
	{
		if (USizeBoxSlot* RowSlot = Cast<USizeBoxSlot>(SubjectRow->Slot))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Center);
			RowSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	// 바깥 SizeBox의 높이가 아이콘의 Desired Size까지 늘리지 않도록 한다.
	if (SubjectIconContainer)
	{
		if (UHorizontalBoxSlot* IconContainerSlot = Cast<UHorizontalBoxSlot>(SubjectIconContainer->Slot))
		{
			IconContainerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			IconContainerSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (SubjectIcon)
	{
		if (USizeBoxSlot* IconSlot = Cast<USizeBoxSlot>(SubjectIcon->Slot))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	ClearSubjectData();
}

void UGifSubjectWidget::SetSubjectData(const FText& Text, bool bShowIcon, UTexture2D* IconTexture)
{
	ClearSubjectData();
	if (SubjectText) SubjectText->SetText(Text);
	const bool bHasIcon = bShowIcon && IsValid(IconTexture) && SubjectIcon && SubjectIconContainer;
	if (bHasIcon)
	{
		SubjectIcon->SetBrushFromTexture(IconTexture, true);

		// ScaleBox에서 그리기만 축소하면 슬롯에 여백이 남으므로,
		// SizeBox의 최대 크기에 맞춘 종횡비를 이미지의 Desired Size에도 적용한다.
		const FVector2D TextureSize(FMath::Max(IconTexture->GetSizeX(), 1), FMath::Max(IconTexture->GetSizeY(), 1));
		double IconScale = 1.0;
		if (SubjectIconContainer->IsMaxDesiredWidthOverride())
		{
			IconScale = FMath::Min(IconScale, FMath::Max(0.0, static_cast<double>(SubjectIconContainer->GetMaxDesiredWidth())) / TextureSize.X);
		}
		if (SubjectIconContainer->IsMaxDesiredHeightOverride())
		{
			IconScale = FMath::Min(IconScale, FMath::Max(0.0, static_cast<double>(SubjectIconContainer->GetMaxDesiredHeight())) / TextureSize.Y);
		}
		SubjectIcon->SetDesiredSizeOverride(TextureSize * IconScale);
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
