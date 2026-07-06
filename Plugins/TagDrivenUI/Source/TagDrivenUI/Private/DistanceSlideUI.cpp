// Fill out your copyright notice in the Description page of Project Settings.


#include "DistanceSlideUI.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void UDistanceSlideUI::NativeConstruct()
{
	Super::NativeConstruct();

	if (LimitOverImage)
	{
		DistanceLimitMID = LimitOverImage->GetDynamicMaterial();

		if (!DistanceLimitMID && DistanceLimitMaterial)
		{
			DistanceLimitMID = UMaterialInstanceDynamic::Create(DistanceLimitMaterial, this);
			LimitOverImage->SetBrushFromMaterial(DistanceLimitMID);
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[DistanceSlideUI][NativeConstruct] MID not created. LimitOverImageValid=%d MaterialValid=%d"),
			LimitOverImage ? 1 : 0,
			DistanceLimitMaterial ? 1 : 0
		);
	}

	CacheSlideRatioImageBasePosition();
}

void UDistanceSlideUI::UpdateDistanceRatio(float InRatio)
{
	CurrentDistanceRatio = FMath::Clamp(InRatio, 0.0f, 1.0f);
	const bool bLimitOver = InRatio > 1.0f;

	UpdateSlideRatioImagePosition();

	UpdateLimitOverMaterial(bLimitOver);
}

void UDistanceSlideUI::UpdateSlideRatioImagePosition()
{
	if (!SlideRatioImage || !SlideBarProgressBar)
	{
		return;
	}

	const FGeometry& BarGeometry = SlideBarProgressBar->GetCachedGeometry();
	const FVector2D BarLocalSize = BarGeometry.GetLocalSize();
	const float CachedBarWidth = BarLocalSize.X;
	const float BarWidth = CachedBarWidth > 0.0f ? CachedBarWidth : SlideBarFallbackWidth;
	const FVector2D BarRatioLocalPosition(
		BarWidth * CurrentDistanceRatio,
		BarLocalSize.Y * 0.5f
	);
	const FVector2D BarRatioAbsolutePosition = BarGeometry.LocalToAbsolute(BarRatioLocalPosition);

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(SlideRatioImage->Slot))
	{
		if (const UPanelWidget* ParentPanel = SlideRatioImage->GetParent())
		{
			const FVector2D ParentLocalPosition =
				ParentPanel->GetCachedGeometry().AbsoluteToLocal(BarRatioAbsolutePosition);
			FVector2D Position = CanvasSlot->GetPosition();
			Position.X = ParentLocalPosition.X + SlideRatioImageOffsetX;
			CanvasSlot->SetPosition(Position);
		}
		return;
	}

	const float MarkerX = BarWidth * CurrentDistanceRatio + SlideRatioImageOffsetX;
	SlideRatioImage->SetRenderTranslation(FVector2D(MarkerX, 0.0f));
}

void UDistanceSlideUI::UpdateLimitOverMaterial(bool bLimitOver)
{
	if (!DistanceLimitMID)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[DistanceSlideUI][UpdateLimitOverMaterial] Skip: DistanceLimitMID is null. bLimitOver=%d Material=%s LimitOverImage=%s"),
			bLimitOver ? 1 : 0,
			*GetNameSafe(DistanceLimitMaterial),
			*GetNameSafe(LimitOverImage)
		);
		return;
	}

	if (bHasCachedLimitOverState && bCachedLimitOverState == bLimitOver)
	{
		return;
	}

	const float ParamValue = bLimitOver ? 1.0f : 0.0f;
	DistanceLimitMID->SetScalarParameterValue(LimitOverParameterName, ParamValue);

	bCachedLimitOverState = bLimitOver;
	bHasCachedLimitOverState = true;
}

void UDistanceSlideUI::CacheSlideRatioImageBasePosition()
{
	if (!SlideRatioImage)
	{
		return;
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(SlideRatioImage->Slot))
	{
		SlideRatioImageBasePosition = CanvasSlot->GetPosition();
	}
	else
	{
		SlideRatioImageBasePosition = FVector2D::ZeroVector;
	}

	bHasCachedSlideRatioImageBasePosition = true;
}
