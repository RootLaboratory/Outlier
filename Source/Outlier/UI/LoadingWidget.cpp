// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LoadingWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"

void ULoadingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoadingProgress && MaterialInstance)
	{
		DynamicMaterialInstance = UMaterialInstanceDynamic::Create(MaterialInstance, this);
		LoadingProgress->SetBrushFromMaterial(DynamicMaterialInstance);
	}

	if (GangTongImage)
	{
		GangTongInitialTranslation = GangTongImage->GetRenderTransform().Translation;
	}

	UpdateProgressMaterial();
}

void ULoadingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateGangTongAnimation(InDeltaTime);
}

void ULoadingWidget::SetDescriptionText(const FText& NewDescription)
{
	if (Description)
	{
		Description->SetText(NewDescription);
	}
}

void ULoadingWidget::SetLoadingProgress(float NewProgress)
{
	LoadingProgressValue = FMath::Clamp(NewProgress, 0.0f, 1.0f);
	UpdateProgressMaterial();
}

void ULoadingWidget::UpdateProgressMaterial()
{
	if (DynamicMaterialInstance)
	{
		DynamicMaterialInstance->SetScalarParameterValue(ProgressParameterName, LoadingProgressValue);
	}
}

void ULoadingWidget::UpdateGangTongAnimation(float InDeltaTime)
{
	if (!GangTongImage)
	{
		return;
	}

	GangTongElapsedTime += InDeltaTime;

	FWidgetTransform Transform = GangTongImage->GetRenderTransform();
	Transform.Translation = GangTongInitialTranslation;
	Transform.Translation.Y += FMath::Sin(GangTongElapsedTime * GangTongBobSpeed) * GangTongBobAmplitude;

	GangTongImage->SetRenderTransform(Transform);
}
