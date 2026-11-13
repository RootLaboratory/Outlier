// Fill out your copyright notice in the Description page of Project Settings.

#include "PartnerLeftHudWidget.h"

#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/RetainerBox.h"
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

	// Brush 가 Material 이면 MID 로 바꿔 캐싱한다. 이후 다운 상태(기본 해제)를 바로 반영한다.
	PartnerImageMID = Partner->GetDynamicMaterial();
	ApplyKnockDownVisual();
}

void UPartnerLeftHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bKnockedDown)
	{
		UpdateKnockDownProgress();
	}
}

void UPartnerLeftHudWidget::SetKnockedDown(bool bInKnockedDown, float InGrayoutAmount)
{
	bKnockedDown = bInKnockedDown;
	KnockDownGrayoutAmount = FMath::Clamp(InGrayoutAmount, 0.0f, 1.0f);
	ApplyKnockDownVisual();
}

void UPartnerLeftHudWidget::SetKnockDownTiming(float StartWorldTime, float Duration)
{
	KnockDownStartTime = StartWorldTime;
	KnockDownDuration = FMath::Max(0.0f, Duration);
	if (bKnockedDown)
	{
		UpdateKnockDownProgress();
	}
}

void UPartnerLeftHudWidget::ApplyKnockDownVisual()
{
	// 다운이 아니면 Retainer 를 꺼서 Effect 와 렌더 타깃 비용을 없앤다.
	if (GrayoutRetainer)
	{
		GrayoutRetainer->SetRetainRendering(bKnockedDown);
		if (UMaterialInstanceDynamic* GrayoutMID = GrayoutRetainer->GetEffectMaterial())
		{
			GrayoutMID->SetScalarParameterValue(GrayAmountParameterName, bKnockedDown ? KnockDownGrayoutAmount : 0.0f);
		}
	}

	if (!PartnerImageMID)
	{
		return;
	}
	if (bKnockedDown)
	{
		PartnerImageMID->SetScalarParameterValue(GrayAmountParameterName, KnockDownGrayoutAmount);
		// 표시되는 첫 프레임부터 올바른 진행도로 그린다.
		UpdateKnockDownProgress();
	}
	else
	{
		// 해제(재부팅 완료 / 중간 해제) 시 원본 이미지 상태로 즉시 되돌린다.
		PartnerImageMID->SetScalarParameterValue(GrayAmountParameterName, 0.0f);
		PartnerImageMID->SetScalarParameterValue(KnockDownProgressParameterName, 1.0f);
	}
}

void UPartnerLeftHudWidget::UpdateKnockDownProgress()
{
	if (!PartnerImageMID)
	{
		return;
	}

	// 0: 다운 직후(회색 + 왜곡 최대) -> 1: 재부팅 완료(원본 색, 왜곡 없음).
	float Progress = 0.0f;
	const UWorld* World = GetWorld();
	if (World && KnockDownDuration > 0.0f)
	{
		const float Linear = FMath::Clamp((World->GetTimeSeconds() - KnockDownStartTime) / KnockDownDuration, 0.0f, 1.0f);
		// 0과 1은 그대로라 재부팅 종료 시점에 정확히 1이 된다.
		Progress = FMath::InterpEaseInOut(0.0f, 1.0f, Linear, FMath::Max(1.0f, KnockDownProgressEaseExponent));
	}
	PartnerImageMID->SetScalarParameterValue(KnockDownProgressParameterName, Progress);
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
