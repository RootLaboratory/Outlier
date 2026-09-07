#include "UI/HackCircleBorderWidget.h"

#include "Components/Border.h"
#include "Widgets/SWidget.h"

void UHackCircleBorderWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BorderWidget && BrushMaterial)
	{
		BorderWidget->SetBrushFromMaterial(BrushMaterial);
	}

	if (BorderWidget)
	{
		if (UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial())
		{
			MID->SetScalarParameterValue(TEXT("GapStart"), 0.25f);
		}
	}

	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::SetAngle(float NewAngleDegrees)
{
	AngleDegrees = FMath::Fmod(NewAngleDegrees, 360.0f);
	if (AngleDegrees < 0.0f)
	{
		AngleDegrees += 360.0f;
	}

	UpdateRotationMaterialParameter();
}

void UHackCircleBorderWidget::SetAmount(float NewAmount)
{
	Amount = NewAmount;
	UpdateGapSizeMaterialParameter();
}

void UHackCircleBorderWidget::SetRotateEnabled(bool bNewRotate)
{
	bRotate = bNewRotate;
}

void UHackCircleBorderWidget::SetInnerRadius(float NewInnerRadius)
{
	InnerRadius = NewInnerRadius;
	UpdateInnerRadiusMaterialParameter();
}

void UHackCircleBorderWidget::SetOuterRadius(float NewOuterRadius)
{
	OuterRadius = NewOuterRadius;
	UpdateOuterRadiusMaterialParameter();
}

void UHackCircleBorderWidget::SetBrushMaterial(UMaterialInterface* NewMaterial)
{
	BrushMaterial = NewMaterial;

	if (BorderWidget && BrushMaterial)
	{
		BorderWidget->SetBrushFromMaterial(BrushMaterial);
	}

	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::RefreshMaterialParameters()
{
	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::UpdateMaterialParameters()
{
	if (!BorderWidget)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial();
	if (!MID)
	{
		return;
	}

	MID->SetScalarParameterValue(TEXT("Rotation"), FMath::Fmod(0.5f - AngleDegrees / 360.0f + 1.0f, 1.0f));
	MID->SetScalarParameterValue(TEXT("GapSize"), Amount);
	MID->SetScalarParameterValue(TEXT("InnerRadius"), InnerRadius);
	MID->SetScalarParameterValue(TEXT("OuterRadius"), OuterRadius);

	InvalidateBorderPaint();
}

void UHackCircleBorderWidget::UpdateRotationMaterialParameter()
{
	if (!BorderWidget)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial();
	if (!MID)
	{
		return;
	}

	MID->SetScalarParameterValue(TEXT("Rotation"), FMath::Fmod(0.5f - AngleDegrees / 360.0f + 1.0f, 1.0f));
	InvalidateBorderPaint();
}

void UHackCircleBorderWidget::UpdateGapSizeMaterialParameter()
{
	if (!BorderWidget)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial();
	if (!MID)
	{
		return;
	}

	MID->SetScalarParameterValue(TEXT("GapSize"), Amount);
	InvalidateBorderPaint();
}

void UHackCircleBorderWidget::UpdateInnerRadiusMaterialParameter()
{
	if (!BorderWidget)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial();
	if (!MID)
	{
		return;
	}

	MID->SetScalarParameterValue(TEXT("InnerRadius"), InnerRadius);
	InvalidateBorderPaint();
}

void UHackCircleBorderWidget::UpdateOuterRadiusMaterialParameter()
{
	if (!BorderWidget)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = BorderWidget->GetDynamicMaterial();
	if (!MID)
	{
		return;
	}

	MID->SetScalarParameterValue(TEXT("OuterRadius"), OuterRadius);
	InvalidateBorderPaint();
}

void UHackCircleBorderWidget::InvalidateBorderPaint()
{
	if (const TSharedPtr<SWidget> SlateWidget = BorderWidget->GetCachedWidget())
	{
		SlateWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}
