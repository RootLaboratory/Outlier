#include "UI/HackCircleBorderWidget.h"

#include "Components/Border.h"
#include "Widgets/SWidget.h"

void UHackCircleBorderWidget::NativeConstruct()
{
	Super::NativeConstruct();

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

	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::SetAmount(float NewAmount)
{
	Amount = NewAmount;
	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::SetRotateEnabled(bool bNewRotate)
{
	bRotate = bNewRotate;
}

void UHackCircleBorderWidget::SetInnerRadius(float NewInnerRadius)
{
	InnerRadius = NewInnerRadius;
	UpdateMaterialParameters();
}

void UHackCircleBorderWidget::SetOuterRadius(float NewOuterRadius)
{
	OuterRadius = NewOuterRadius;
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

	if (const TSharedPtr<SWidget> SlateWidget = BorderWidget->GetCachedWidget())
	{
		SlateWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}
