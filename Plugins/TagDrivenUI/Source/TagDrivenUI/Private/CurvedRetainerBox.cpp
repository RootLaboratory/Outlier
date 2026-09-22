// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurvedRetainerBox.h"

#include "Materials/MaterialInstanceDynamic.h"

void UCurvedRetainerBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::RefreshCurvedMaterial()
{
	if (!CurvedEffectMaterial)
	{
		CurvedEffectMaterialInstance = nullptr;
		CachedCurvedEffectMaterial = nullptr;
		SetEffectMaterial(nullptr);
		return;
	}

	if (!CurvedEffectMaterialInstance || CachedCurvedEffectMaterial != CurvedEffectMaterial)
	{
		CurvedEffectMaterialInstance = UMaterialInstanceDynamic::Create(CurvedEffectMaterial, this);
		CachedCurvedEffectMaterial = CurvedEffectMaterial;
	}

	if (TextureParameterName != NAME_None)
	{
		SetTextureParameter(TextureParameterName);
	}

	if (CurvatureParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			CurvatureParameterName,
			bEnableCurvature ? CurvatureDegrees : 0.0f);
	}

	if (HorizontalArcParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			HorizontalArcParameterName,
			FMath::Clamp(HorizontalArcDegrees, 1.0f, 175.0f));
	}

	if (VerticalArcParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			VerticalArcParameterName,
			FMath::Clamp(VerticalArcDegrees, 1.0f, 175.0f));
	}

	if (SourceHFovParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			SourceHFovParameterName,
			FMath::Clamp(SourceHFovDegrees, 1.0f, 175.0f));
	}

	if (SourceVFovParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			SourceVFovParameterName,
			FMath::Clamp(SourceVFovDegrees, 1.0f, 175.0f));
	}

	if (WarpAmountParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			WarpAmountParameterName,
			bEnableCurvature ? FMath::Clamp(WarpAmount, 0.0f, 1.0f) : 0.0f);
	}

	if (TextureScaleParameterName != NAME_None)
	{
		CurvedEffectMaterialInstance->SetScalarParameterValue(
			TextureScaleParameterName,
			FMath::Max(TextureScale, 0.01f));
	}

	if (ViewportRegionParameterName != NAME_None)
	{
		const FVector2D SafeRegionScale(
			FMath::Max(ViewportRegionScale.X, 0.0001f),
			FMath::Max(ViewportRegionScale.Y, 0.0001f));

		CurvedEffectMaterialInstance->SetVectorParameterValue(
			ViewportRegionParameterName,
			FLinearColor(
				ViewportRegionOffset.X,
				ViewportRegionOffset.Y,
				SafeRegionScale.X,
				SafeRegionScale.Y));
	}

	SetEffectMaterial(CurvedEffectMaterialInstance);
}

void UCurvedRetainerBox::SetCurvatureDegrees(const float InDegrees)
{
	CurvatureDegrees = FMath::Clamp(InDegrees, 0.0f, 60.0f);
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::SetCurvatureEnabled(const bool bInEnabled)
{
	bEnableCurvature = bInEnabled;
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::SetSphericalProjectionDegrees(
	const float InHorizontalArcDegrees,
	const float InVerticalArcDegrees,
	const float InSourceHFovDegrees,
	const float InSourceVFovDegrees)
{
	HorizontalArcDegrees = FMath::Clamp(InHorizontalArcDegrees, 1.0f, 175.0f);
	VerticalArcDegrees = FMath::Clamp(InVerticalArcDegrees, 1.0f, 175.0f);
	SourceHFovDegrees = FMath::Clamp(InSourceHFovDegrees, 1.0f, 175.0f);
	SourceVFovDegrees = FMath::Clamp(InSourceVFovDegrees, 1.0f, 175.0f);
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::SetWarpAmount(const float InWarpAmount)
{
	WarpAmount = FMath::Clamp(InWarpAmount, 0.0f, 1.0f);
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::SetTextureScale(const float InTextureScale)
{
	TextureScale = FMath::Max(InTextureScale, 0.01f);
	RefreshCurvedMaterial();
}

void UCurvedRetainerBox::SetViewportRegion(const FVector2D InOffset, const FVector2D InScale)
{
	ViewportRegionOffset = InOffset;
	ViewportRegionScale = FVector2D(
		FMath::Max(InScale.X, 0.0001f),
		FMath::Max(InScale.Y, 0.0001f));
	RefreshCurvedMaterial();
}
