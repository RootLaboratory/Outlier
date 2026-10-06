#include "UI/ClickCircleMiniGameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "UI/HackCircleBorderWidget.h"

void UClickCircleMiniGameWidget::OnMiniGameStarted()
{
	Super::OnMiniGameStarted();

	EnsureCircleCanvas();
	SpawnCircleWidgets();
	CurrentCircleIndex = ClickCircleWidgets.IsEmpty() ? INDEX_NONE : 0;
	RandomizeSpinningPointer();
	RefreshCircleVisuals();
}

void UClickCircleMiniGameWidget::OnMiniGameUpdated(float DeltaTime)
{
	Super::OnMiniGameUpdated(DeltaTime);

	UpdateSpinningPointer(DeltaTime);
}

void UClickCircleMiniGameWidget::OnMiniGameFinishedEvent(EHackResult Result)
{
	CurrentCircleIndex = INDEX_NONE;
	ClearCircleWidgets();
	FinishMiniGame(Result);
	Super::OnMiniGameFinishedEvent(Result);
}

bool UClickCircleMiniGameWidget::HandlePrimaryClick()
{
	if (!IsMiniGameActive())
	{
		return false;
	}

	const EClickCircleMouseResult ClickResult = EvaluateMouseClick();
	if (ClickResult == EClickCircleMouseResult::Clear)
	{
		FinishMiniGame(EHackResult::Success);
	}
	else if (ClickResult == EClickCircleMouseResult::Valid)
	{
		AdvanceToNextCircle();
	}
	else
	{
		FinishMiniGame(EHackResult::Fail);
	}

	return true;
}

void UClickCircleMiniGameWidget::EnsureCircleCanvas()
{
	if (CircleCanvas || !WidgetTree)
	{
		return;
	}

	CircleCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CircleCanvas"));
	WidgetTree->RootWidget = CircleCanvas;
}

void UClickCircleMiniGameWidget::SpawnCircleWidgets()
{
	if (!CircleCanvas || !CircleBorderWidgetClass)
	{
		return;
	}

	ClearCircleWidgets();

	const int32 MinCount = FMath::Max(2, MinCircleCount);
	const int32 MaxCount = FMath::Max(MinCount, MaxCircleCount);
	const int32 CircleCount = FMath::RandRange(MinCount, MaxCount);

	const float SafeBackgroundDiameter = FMath::Max(BackgroundCircleDiameter, 1.0f);
	const float HalfThickness = FMath::Clamp(CircleThickness, 0.0f, SafeBackgroundDiameter - 1.0f) * 0.5f;
	const float BackgroundMaxRadius = SafeBackgroundDiameter * 0.5f - HalfThickness;
	const float EffectiveMinRadius = FMath::Min(FMath::Max(MinCircleRadius, HalfThickness), BackgroundMaxRadius);
	const float EffectiveMaxRadius = FMath::Min(FMath::Max(MaxCircleRadius, EffectiveMinRadius), BackgroundMaxRadius);
	const float RadiusInterval = CircleCount > 1
		? (EffectiveMaxRadius - EffectiveMinRadius) / static_cast<float>(CircleCount - 1)
		: 0.0f;

	for (int32 Index = 0; Index < CircleCount; ++Index)
	{
		const float Radius = EffectiveMaxRadius - RadiusInterval * static_cast<float>(Index);
		const float ValidAngleDegrees = FMath::RandRange(0.0f, 360.0f);

		FClickCircleWidget& ClickCircle = ClickCircleWidgets.AddDefaulted_GetRef();
		ClickCircle.BaseBorderWidget = CreateCircleBorderLayer(Radius, 0.0f, 0.0f, BaseCircleMaterial);
		ClickCircle.ValidBorderWidget = CreateCircleBorderLayer(Radius, ClickAngleToleranceDegrees / 360.0f, ValidAngleDegrees, ValidCircleMaterial);
		ClickCircle.PointerBorderWidget = CreateCircleBorderLayer(Radius, PointerArcDegrees / 360.0f, StartAngleDegrees, PointerCircleMaterial);
		ClickCircle.ClickRadius = Radius;
		ClickCircle.ValidAngleDegrees = ValidAngleDegrees;
		ClickCircle.bCleared = false;
	}
}

void UClickCircleMiniGameWidget::ClearCircleWidgets()
{
	for (FClickCircleWidget& ClickCircle : ClickCircleWidgets)
	{
		if (ClickCircle.BaseBorderWidget)
		{
			ClickCircle.BaseBorderWidget->RemoveFromParent();
		}

		if (ClickCircle.ValidBorderWidget)
		{
			ClickCircle.ValidBorderWidget->RemoveFromParent();
		}

		if (ClickCircle.PointerBorderWidget)
		{
			ClickCircle.PointerBorderWidget->RemoveFromParent();
		}
	}

	ClickCircleWidgets.Reset();
}

UHackCircleBorderWidget* UClickCircleMiniGameWidget::CreateCircleBorderLayer(float Radius, float Amount, float AngleDegrees, UMaterialInterface* Material)
{
	if (!CircleCanvas || !CircleBorderWidgetClass)
	{
		return nullptr;
	}

	UHackCircleBorderWidget* CircleBorder = CreateWidget<UHackCircleBorderWidget>(GetOwningPlayer(), CircleBorderWidgetClass);
	if (!CircleBorder)
	{
		return nullptr;
	}

	CircleBorder->SetRotateEnabled(false);
	CircleBorder->SetBrushMaterial(Material);
	CircleBorder->SetAmount(FMath::Clamp(Amount, 0.0f, 1.0f));
	CircleBorder->SetAngle(AngleDegrees - 90.0f - Amount * 180.0f);

	const float HalfThickness = FMath::Max(CircleThickness, 0.0f) * 0.5f;
	const float LayerDiameter = FMath::Max((Radius + HalfThickness) * 2.0f, 1.0f);
	UCanvasPanelSlot* CanvasSlot = CircleCanvas->AddChildToCanvas(CircleBorder);
	if (CanvasSlot)
	{
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetPosition(CircleCenter);
		CanvasSlot->SetSize(FVector2D(LayerDiameter, LayerDiameter));
	}

	CircleBorder->SetInnerRadius(FMath::Max(Radius - HalfThickness, 0.0f) / LayerDiameter);
	CircleBorder->SetOuterRadius((Radius + HalfThickness) / LayerDiameter);

	return CircleBorder;
}

void UClickCircleMiniGameWidget::UpdateSpinningPointer(float DeltaTime)
{
	if (!ClickCircleWidgets.IsValidIndex(CurrentCircleIndex))
	{
		return;
	}

	const float DirectionSign = SpinningPointer.Direction == EClickCirclePointerDirection::Clockwise ? 1.0f : -1.0f;
	const float SpeedScalePerIndex = GetPointerSpeedScale();
	SpinningPointer.AngleDegrees = FMath::Fmod(SpinningPointer.AngleDegrees + SpinningPointer.Speed * SpeedScalePerIndex * DirectionSign * DeltaTime, 360.0f);
	if (SpinningPointer.AngleDegrees < 0.0f)
	{
		SpinningPointer.AngleDegrees += 360.0f;
	}

	if (FClickCircleWidget* CurrentCircle = ClickCircleWidgets.IsValidIndex(CurrentCircleIndex) ? &ClickCircleWidgets[CurrentCircleIndex] : nullptr)
	{
		if (CurrentCircle->PointerBorderWidget)
		{
			CurrentCircle->PointerBorderWidget->SetAngle(SpinningPointer.AngleDegrees - 90.0f - PointerArcDegrees * 0.5f);
		}
	}
}

void UClickCircleMiniGameWidget::AdvanceToNextCircle()
{
	if (!ClickCircleWidgets.IsValidIndex(CurrentCircleIndex))
	{
		return;
	}

	ClickCircleWidgets[CurrentCircleIndex].bCleared = true;
	++CurrentCircleIndex;

	if (!ClickCircleWidgets.IsValidIndex(CurrentCircleIndex))
	{
		FinishMiniGame(EHackResult::Success);
		return;
	}

	RandomizeSpinningPointer();
	RefreshCircleVisuals();
}

void UClickCircleMiniGameWidget::RefreshCircleVisuals()
{
	for (int32 Index = 0; Index < ClickCircleWidgets.Num(); ++Index)
	{
		FClickCircleWidget& ClickCircle = ClickCircleWidgets[Index];
		const bool bIsCurrent = Index == CurrentCircleIndex;
		if (ClickCircle.ValidBorderWidget)
		{
			ClickCircle.ValidBorderWidget->SetVisibility(bIsCurrent ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}

		if (ClickCircle.PointerBorderWidget)
		{
			ClickCircle.PointerBorderWidget->SetVisibility(bIsCurrent ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			ClickCircle.PointerBorderWidget->SetAngle(SpinningPointer.AngleDegrees - 90.0f - PointerArcDegrees * 0.5f);
		}
	}
}

void UClickCircleMiniGameWidget::RandomizeSpinningPointer()
{
	const float MinSpeed = FMath::Min(MinRotationSpeedDegrees, MaxRotationSpeedDegrees);
	const float MaxSpeed = FMath::Max(MinRotationSpeedDegrees, MaxRotationSpeedDegrees);

	SpinningPointer.Speed = FMath::RandRange(MinSpeed, MaxSpeed);
	SpinningPointer.Direction = FMath::RandBool()
		? EClickCirclePointerDirection::Clockwise
		: EClickCirclePointerDirection::CounterClockwise;
	SpinningPointer.AngleDegrees = StartAngleDegrees;
}

EClickCircleMouseResult UClickCircleMiniGameWidget::EvaluateMouseClick() const
{
	if (!ClickCircleWidgets.IsValidIndex(CurrentCircleIndex))
	{
		return EClickCircleMouseResult::Failed;
	}

	const FClickCircleWidget& CurrentCircle = ClickCircleWidgets[CurrentCircleIndex];
	const float PointerDistance = GetAngularDistanceDegrees(SpinningPointer.AngleDegrees, CurrentCircle.ValidAngleDegrees);
	if (PointerDistance > ClickAngleToleranceDegrees * 0.5f)
	{
		return EClickCircleMouseResult::Failed;
	}

	return CurrentCircleIndex >= ClickCircleWidgets.Num() - 1
		? EClickCircleMouseResult::Clear
		: EClickCircleMouseResult::Valid;
}

FVector2D UClickCircleMiniGameWidget::GetCircleCenterLocal() const
{
	if (!CircleCanvas)
	{
		return CircleCenter;
	}

	return CircleCanvas->GetCachedGeometry().GetLocalSize() * 0.5f + CircleCenter;
}

float UClickCircleMiniGameWidget::GetAngularDistanceDegrees(float A, float B) const
{
	const float Delta = FMath::Fmod(A - B + 540.0f, 360.0f) - 180.0f;
	return FMath::Abs(Delta);
}

float UClickCircleMiniGameWidget::GetPointerSpeedScale() const
{
	if (!ClickCircleWidgets.IsValidIndex(CurrentCircleIndex) || ClickCircleWidgets.Num() <= 1)
	{
		return 1.0f;
	}

	const float MinScale = FMath::Max(0.0f, FMath::Min(MinSpeedScalePerIndex, MaxSpeedScalePerIndex));
	const float MaxScale = FMath::Max(MinScale, FMath::Max(MinSpeedScalePerIndex, MaxSpeedScalePerIndex));
	const float IndexAlpha = static_cast<float>(CurrentCircleIndex) / static_cast<float>(ClickCircleWidgets.Num() - 1);
	return FMath::Lerp(MaxScale, MinScale, IndexAlpha);
}
