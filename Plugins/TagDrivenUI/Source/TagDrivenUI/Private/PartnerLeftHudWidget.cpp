// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerLeftHudWidget.h"

#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TagDrivenUIGameplayTags.h"

void UPartnerLeftHudWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!DefaultShooterConditionTag.IsValid())
	{
		DefaultShooterConditionTag = TagDrivenUITags::Condition::Shooter::HP();
	}

	if (!CurrentShooterConditionTag.IsValid())
	{
		CurrentShooterConditionTag = DefaultShooterConditionTag;
	}

	InitializeShooterConditionMaterial();
	UpdateShooterConditionMaterial();

	if (!bDistanceTrianglePositionCached)
	{
		if (UCanvasPanelSlot* TriangleSlot = Cast<UCanvasPanelSlot>(Dist_Triangle->Slot))
		{
			DistanceTriangleBasePosition = TriangleSlot->GetPosition();
			bDistanceTrianglePositionCached = true;
		}
	}
	DistanceLimitMID = Signal->GetDynamicMaterial();
	if (!DistanceLimitMID && DistanceLimitMaterial)
	{
		DistanceLimitMID = UMaterialInstanceDynamic::Create(DistanceLimitMaterial, this);
		Signal->SetBrushFromMaterial(DistanceLimitMID);
	}
	bDistanceLimitVisualInitialized = false;
	UpdateDistanceLimitVisual(bDistanceLimitOver);
	UpdateDistanceTrianglePosition();
}

void UPartnerLeftHudWidget::SetShooterCondition(FGameplayTag InConditionTag)
{
	CurrentShooterConditionTag = InConditionTag.IsValid()
		? InConditionTag
		: DefaultShooterConditionTag;

	UpdateShooterConditionMaterial();
}

void UPartnerLeftHudWidget::RefreshShooterConditionUI()
{
	SetShooterCondition(DefaultShooterConditionTag);
}

void UPartnerLeftHudWidget::UpdateDistanceRatio(float InRatio)
{
	const float NearRatio = FMath::Clamp(DistanceTriangleNearRatio, 0.0f, 0.95f);
	CurrentDistanceRatio = FMath::Clamp((InRatio - NearRatio) / (1.0f - NearRatio), 0.0f, 1.0f);
	UpdateDistanceTrianglePosition();
	UpdateDistanceLimitVisual(InRatio > 1.0f);
}

void UPartnerLeftHudWidget::UpdateDistanceLimitVisual(bool bOverLimit)
{
	const bool bStateChanged = bDistanceLimitOver != bOverLimit;
	bDistanceLimitOver = bOverLimit;
	if (!DistanceLimitMID || (bDistanceLimitVisualInitialized && !bStateChanged))
	{
		return;
	}

	DistanceLimitMID->SetScalarParameterValue(DistanceLimitParameterName, bOverLimit ? 1.0f : 0.0f);
	bDistanceLimitVisualInitialized = true;
}

void UPartnerLeftHudWidget::UpdateDistanceTrianglePosition()
{
	UCanvasPanelSlot* TriangleSlot = Cast<UCanvasPanelSlot>(Dist_Triangle->Slot);
	UCanvasPanelSlot* DistanceSlot = Cast<UCanvasPanelSlot>(Distance->Slot);
	if (!TriangleSlot || !DistanceSlot)
	{
		return;
	}

	if (!bDistanceTrianglePositionCached)
	{
		DistanceTriangleBasePosition = TriangleSlot->GetPosition();
		bDistanceTrianglePositionCached = true;
	}

	const float TravelWidth = DistanceTriangleTravelWidth > 0.0f
		? DistanceTriangleTravelWidth
		: DistanceSlot->GetSize().X;
	if (TravelWidth <= 0.0f)
	{
		return;
	}

	FVector2D Position = DistanceTriangleBasePosition;
	Position.X += DistanceTriangleOffsetX + TravelWidth * CurrentDistanceRatio;
	TriangleSlot->SetPosition(Position);
}

void UPartnerLeftHudWidget::InitializeShooterConditionMaterial()
{
	if (ShooterConditionMID)
	{
		return;
	}

	ShooterConditionMID = ShooterConditionUI->GetDynamicMaterial();
	if (!ShooterConditionMID && ShooterConditionMaterial)
	{
		ShooterConditionMID = UMaterialInstanceDynamic::Create(ShooterConditionMaterial, this);
		ShooterConditionUI->SetBrushFromMaterial(ShooterConditionMID);
	}
}

void UPartnerLeftHudWidget::UpdateShooterConditionMaterial()
{
	InitializeShooterConditionMaterial();
	if (ShooterConditionMID)
	{
		ShooterConditionMID->SetScalarParameterValue(
			ShooterConditionParameterName,
			GetShooterConditionMaterialValue(CurrentShooterConditionTag));
	}
}

float UPartnerLeftHudWidget::GetShooterConditionMaterialValue(const FGameplayTag& InConditionTag) const
{
	if (const float* FoundValue = ConditionMaterialValues.Find(InConditionTag))
	{
		return *FoundValue;
	}

	if (InConditionTag.MatchesTagExact(TagDrivenUITags::Condition::Shooter::Shield()))
	{
		return 0.0f;
	}

	if (InConditionTag.MatchesTagExact(TagDrivenUITags::Condition::Shooter::PartnerShield()))
	{
		return 2.0f;
	}

	return 1.0f;
}
