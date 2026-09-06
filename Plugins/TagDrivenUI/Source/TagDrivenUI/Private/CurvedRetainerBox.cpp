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
			bEnableCurvature ? 1.0f : 0.0f);
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
