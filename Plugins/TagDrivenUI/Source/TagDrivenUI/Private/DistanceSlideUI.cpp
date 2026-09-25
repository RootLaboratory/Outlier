// Fill out your copyright notice in the Description page of Project Settings.


#include "DistanceSlideUI.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"
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
	if (!SlideCanvasPanel || !SlideRatioImage || !SlideBarProgressBar)
	{
	/*	UE_LOG(
			LogTemp,
			Warning,
			TEXT("[DistanceSlideUI][UpdateSlideRatioImagePosition] Skip: Canvas=%s SlideBar=%s SlideRatioImage=%s"),
			*GetNameSafe(SlideCanvasPanel),
			*GetNameSafe(SlideBarProgressBar),
			*GetNameSafe(SlideRatioImage)
		);*/
		return;
	}

	const FGeometry& BarGeometry = SlideBarProgressBar->GetCachedGeometry();
	const FVector2D BarLocalSize = BarGeometry.GetLocalSize();
	const float CachedBarWidth = BarLocalSize.X;
	const float BarWidth = CachedBarWidth > 0.0f ? CachedBarWidth : SlideBarFallbackWidth;
	const FVector2D BarAbsoluteLeft = BarGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D BarAbsoluteRight = BarGeometry.LocalToAbsolute(FVector2D(CachedBarWidth, 0.0f));
	const FGeometry& CanvasGeometry = SlideCanvasPanel->GetCachedGeometry();
	const FVector2D BarLeftInCanvas = CanvasGeometry.AbsoluteToLocal(BarAbsoluteLeft);
	const FVector2D BarRightInCanvas = CanvasGeometry.AbsoluteToLocal(BarAbsoluteRight);
	const float CanvasTravelWidth = FMath::Abs(BarRightInCanvas.X - BarLeftInCanvas.X);
	const bool bBarGeometryHasSize = BarLocalSize.X > 0.0f && BarLocalSize.Y > 0.0f;
	const bool bBarGeometryIsFinite = FMath::IsFinite(BarLocalSize.X) && FMath::IsFinite(BarLocalSize.Y);

	if (UCanvasPanelSlot* CanvasSlot = ResolveSlideRatioCanvasSlot())
	{
		if (!bHasCachedSlideRatioImageBasePosition)
		{
			CacheSlideRatioImageBasePosition();
		}

		const float TravelWidth = CanvasTravelWidth > 0.0f
			? CanvasTravelWidth
			: BarWidth;

		FVector2D Position = SlideRatioImageBasePosition;
		Position.X += SlideRatioImageOffsetX - TravelWidth * CurrentDistanceRatio;
		CanvasSlot->SetPosition(Position);
	
		return;
	}

	const FVector2D RenderTranslation(SlideRatioImageOffsetX - BarWidth * CurrentDistanceRatio, 0.0f);
	
	SlideRatioImage->SetRenderTranslation(RenderTranslation);
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
	if (!SlideCanvasPanel || !SlideRatioImage)
	{
		return;
	}

	if (UCanvasPanelSlot* CanvasSlot = ResolveSlideRatioCanvasSlot())
	{
		SlideRatioImageBasePosition = CanvasSlot->GetPosition();
		bHasCachedSlideRatioImageBasePosition = true;
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[DistanceSlideUI][CacheBasePosition] Image=%s is not contained by Canvas=%s"),
			*GetNameSafe(SlideRatioImage),
			*GetNameSafe(SlideCanvasPanel));
	}
}

UCanvasPanelSlot* UDistanceSlideUI::ResolveSlideRatioCanvasSlot() const
{
	if (!SlideCanvasPanel || !SlideRatioImage)
	{
		return nullptr;
	}

	UWidget* CanvasChild = SlideRatioImage;
	while (CanvasChild)
	{
		UPanelWidget* Parent = CanvasChild->GetParent();
		if (Parent == SlideCanvasPanel)
		{
			return Cast<UCanvasPanelSlot>(CanvasChild->Slot);
		}

		CanvasChild = Parent;
	}

	return nullptr;
}
