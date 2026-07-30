#include "UI/SpinningCircleMiniGameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/HackCircleBorderWidget.h"

void USpinningCircleMiniGameWidget::OnMiniGameStarted()
{
	Super::OnMiniGameStarted();

	EnsureCircleCanvas();
	SpawnCircleBorders();
	bJudgementActive = false;
	bPendingInitialMouseReset = CircleLayers.Num() >= 2;
}

void USpinningCircleMiniGameWidget::OnMiniGameUpdated(float DeltaTime)
{
	Super::OnMiniGameUpdated(DeltaTime);

	UpdateCircleLayers(DeltaTime);

	if (bPendingInitialMouseReset)
	{
		if (TryInitializeMousePosition())
		{
			bPendingInitialMouseReset = false;
			bJudgementActive = true;
		}
		return;
	}

	if (bJudgementActive)
	{
		const EMouseAreaResult MouseAreaResult = EvaluateMouseArea();
		if (MouseAreaResult == EMouseAreaResult::Finished)
		{
			FinishMiniGame(EHackResult::Success);
		}
		else if (MouseAreaResult == EMouseAreaResult::Reset)
		{
			ResetMouseToInitialPosition();
		}
	}
}

void USpinningCircleMiniGameWidget::OnMiniGameFinishedEvent(EHackResult Result)
{
	bJudgementActive = false;
	bPendingInitialMouseReset = false;
	ClearCircleBorders();

	Super::OnMiniGameFinishedEvent(Result);
}

//int32 USpinningCircleMiniGameWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
//{
//	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
//
//	const FVector2D Center = GetCircleCenterLocal();
//
//	// 각 레이어의 갭 시작/끝 각도를 선으로 표시
//	for (const FSpinningCircleLayer& Layer : CircleLayers)
//	{
//		if (Layer.GapSize <= 0.0f) continue;
//
//		const float GapSizeDeg = Layer.GapSize * 360.0f;
//		const float GapStart = FMath::Fmod(90.0f + Layer.AngleDegrees, 360.0f);
//		const float GapEnd = FMath::Fmod(GapStart + GapSizeDeg, 360.0f);
//
//		// 갭 시작 (녹색)
//		const FVector2D StartDir(
//			FMath::Cos(FMath::DegreesToRadians(GapStart)),
//			FMath::Sin(FMath::DegreesToRadians(GapStart)));
//		// 갭 끝 (빨강)
//		const FVector2D EndDir(
//			FMath::Cos(FMath::DegreesToRadians(GapEnd)),
//			FMath::Sin(FMath::DegreesToRadians(GapEnd)));
//
//		TArray<FVector2D> GreenLine = { Center, Center + StartDir * Layer.Radius };
//		TArray<FVector2D> RedLine = { Center, Center + EndDir * Layer.Radius };
//
//		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
//			GreenLine, ESlateDrawEffect::None, FLinearColor::Green, true, 2.f);
//		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
//			RedLine, ESlateDrawEffect::None, FLinearColor::Red, true, 2.f);
//	}
//
//	// 마우스 위치 점
//	const FVector2D MouseLocal = GetMouseLocalPosition();
//	const FVector2D MouseBox(6.f, 6.f);
//	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
//		AllottedGeometry.ToPaintGeometry(MouseBox, FSlateLayoutTransform(MouseLocal - MouseBox * 0.5f)),
//		FCoreStyle::Get().GetBrush("WhiteBrush"),
//		ESlateDrawEffect::None, FLinearColor::Yellow);
//
//	return LayerId;
//}

void USpinningCircleMiniGameWidget::EnsureCircleCanvas()
{
	if (CircleCanvas || !WidgetTree)
	{
		return;
	}

	CircleCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CircleCanvas"));
	WidgetTree->RootWidget = CircleCanvas;
}

void USpinningCircleMiniGameWidget::SpawnCircleBorders()
{
	if (!CircleCanvas || !CircleBorderWidgetClass)
	{
		return;
	}

	ClearCircleBorders();

	const int32 MinCount = FMath::Max(2, MinCircleCount);
	const int32 MaxCount = FMath::Max(MinCount, MaxCircleCount);
	const int32 CircleCount = FMath::RandRange(MinCount, MaxCount);

	const float EffectiveMaxRadius = FMath::Max(MaxCircleRadius, MinCircleRadius + 1.0f);
	const float RadiusInterval = CircleCount > 1
		? (EffectiveMaxRadius - MinCircleRadius) / static_cast<float>(CircleCount - 1)
		: 0.0f;

	for (int32 Index = 0; Index < CircleCount; ++Index)
	{
		UHackCircleBorderWidget* CircleBorder = CreateWidget<UHackCircleBorderWidget>(GetOwningPlayer(), CircleBorderWidgetClass);
		if (!CircleBorder)
		{
			continue;
		}

		const float Radius = EffectiveMaxRadius - RadiusInterval * static_cast<float>(Index);
		const bool bIsFirstCircle = Index == 0;

		const float GapSize = bIsFirstCircle ? 0.0f : FMath::RandRange(MinGapSize, MaxGapSize);
		const bool bSpinning = !bIsFirstCircle;

		const float Direction = FMath::RandBool() ? 1.0f : -1.0f;
		const float RotationSpeed = bSpinning
			? FMath::RandRange(MinRotationSpeedDegrees, MaxRotationSpeedDegrees) * Direction
			: 0.0f;
		const float AngleDegrees = FMath::RandRange(0.0f, 360.0f);
		const FVector2D LayerSize(Radius * 2.0f, Radius * 2.0f);

		CircleBorder->SetRotateEnabled(true);
		CircleBorder->SetAngle(AngleDegrees);
		CircleBorder->SetAmount(GapSize);

		UCanvasPanelSlot* CanvasSlot = CircleCanvas->AddChildToCanvas(CircleBorder);
		if (CanvasSlot)
		{
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetPosition(CircleCenter);
			CanvasSlot->SetSize(LayerSize);
		}

		const float HalfThicknessUV = CircleThickness * 0.5f / (Radius * 2.0f);
		CircleBorder->SetInnerRadius(0.5f - HalfThicknessUV);
		CircleBorder->SetOuterRadius(0.5f + HalfThicknessUV);

		CircleBorders.Add(CircleBorder);

		FSpinningCircleLayer& Layer = CircleLayers.AddDefaulted_GetRef();
		Layer.BorderWidget = CircleBorder;
		Layer.Radius = Radius;
		Layer.GapSize = GapSize;
		Layer.AngleDegrees = AngleDegrees;
		Layer.RotationSpeedDegrees = RotationSpeed;
		Layer.bSpinning = bSpinning;
	}
}

void USpinningCircleMiniGameWidget::ClearCircleBorders()
{
	for (UHackCircleBorderWidget* CircleBorder : CircleBorders)
	{
		if (CircleBorder)
		{
			CircleBorder->RemoveFromParent();
		}
	}

	CircleBorders.Reset();
	CircleLayers.Reset();
}

void USpinningCircleMiniGameWidget::UpdateCircleLayers(float DeltaTime)
{
	for (FSpinningCircleLayer& Layer : CircleLayers)
	{
		if (!Layer.BorderWidget || !Layer.bSpinning)
		{
			continue;
		}

		Layer.AngleDegrees = FMath::Fmod(Layer.AngleDegrees + Layer.RotationSpeedDegrees * DeltaTime, 360.0f);

		if (Layer.AngleDegrees < 0.0f)
		{
			Layer.AngleDegrees += 360.0f;
		}

		Layer.BorderWidget->SetAngle(Layer.AngleDegrees);
	}
}

bool USpinningCircleMiniGameWidget::TryInitializeMousePosition()
{
	if (!CircleCanvas || CircleLayers.Num() < 2)
	{
		return false;
	}

	const FVector2D CanvasSize = CircleCanvas->GetCachedGeometry().GetLocalSize();
	if (CanvasSize.IsNearlyZero())
	{
		return false;
	}

	const float OutermostRadius = CircleLayers[0].Radius;
	const float NextRadius = CircleLayers[1].Radius;

	const float RadiusGapHalf = FMath::Abs(OutermostRadius - NextRadius) * 0.5f;
	/*const float StartRadius = OutermostRadius - RadiusGapHalf;*/
	const float HalfHitTolerance = RingHitTolerance * 0.5f;


	const float StartRadius = (CircleLayers[0].Radius + HalfHitTolerance
		+ CircleLayers[1].Radius + HalfHitTolerance) / 2.0f;

	const float StartRadians = FMath::DegreesToRadians(StartAngleDegrees);

	InitialMouseLocalPosition = GetCircleCenterLocal()
		+ FVector2D(FMath::Cos(StartRadians), FMath::Sin(StartRadians)) * StartRadius;

	ResetMouseToInitialPosition();
	return true;
}

void USpinningCircleMiniGameWidget::ResetMouseToInitialPosition()
{
	if (!CircleCanvas)
	{
		return;
	}

	const FVector2D TargetAbsolute = CircleCanvas->GetCachedGeometry().LocalToAbsolute(InitialMouseLocalPosition);
	FSlateApplication::Get().SetCursorPos(TargetAbsolute);
}

USpinningCircleMiniGameWidget::EMouseAreaResult USpinningCircleMiniGameWidget::EvaluateMouseArea() const
{
	if (CircleLayers.Num() < 2)
	{
		return EMouseAreaResult::Reset;
	}

	const FVector2D CenterLocal = GetCircleCenterLocal();
	const FVector2D ToMouse = GetMouseLocalPosition() - CenterLocal;

	const float MouseRadius = ToMouse.Size();
	const float MouseAngleDegrees = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(ToMouse.Y, ToMouse.X)) + 360.0f, 360.0f);

	//const float OuterLimit = CircleLayers[0].Radius + CircleThickness * 0.5f;
	//const float InnerLimit = CircleLayers.Last().Radius - CircleThickness * 0.5f;

	const float HalfHitTolerance = RingHitTolerance * 0.5f;
	const float HalfCircleThickness = CircleThickness * 0.5f;


	const float OuterLimit = CircleLayers[0].Radius - HalfCircleThickness;
	const float FinishLimit = CircleLayers.Last().Radius - HalfCircleThickness;
	if (MouseRadius >= OuterLimit)
	{
		return EMouseAreaResult::Reset;
	}

	for (const FSpinningCircleLayer& Layer : CircleLayers)
	{
		if (Layer.GapSize <= 0.0f)
		{
			continue;
		}

		if (FMath::Abs(MouseRadius - Layer.Radius) > HalfHitTolerance)
		{
			continue;
		}

		if (!IsAngleInsideGap(Layer, MouseAngleDegrees))
		{
			return EMouseAreaResult::Reset;
		}
	}

	if (MouseRadius <= FinishLimit)
	{
		return EMouseAreaResult::Finished;
	}

	return EMouseAreaResult::Valid;
}

FVector2D USpinningCircleMiniGameWidget::GetCircleCenterLocal() const
{
	if (!CircleCanvas)
	{
		return CircleCenter;
	}

	return CircleCanvas->GetCachedGeometry().GetLocalSize() * 0.5f + CircleCenter;
}

FVector2D USpinningCircleMiniGameWidget::GetMouseLocalPosition() const
{
	if (!CircleCanvas)
	{
		return FVector2D::ZeroVector;
	}

	return CircleCanvas->GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
}

bool USpinningCircleMiniGameWidget::IsAngleInsideGap(const FSpinningCircleLayer& Layer, float MouseAngleDegrees) const
{
	if (Layer.GapSize <= 0.0f)
	{
		return false;
	}

	const float GapSizeDegrees = FMath::Clamp(Layer.GapSize, 0.0f, 1.0f) * 360.0f;

	// 위젯 자체를 AngleDegrees로 회전 → 갭은 머티리얼 고정 위치(아래쪽=+90°)에서 AngleDegrees만큼 회전
	// 갭 시작 위치 = (90° + AngleDegrees) mod 360
	const float GapStartDegrees = FMath::Fmod(90.0f + Layer.AngleDegrees, 360.0f);
	const float DeltaFromStart = FMath::Fmod(MouseAngleDegrees - GapStartDegrees + 360.0f, 360.0f);

	return DeltaFromStart < GapSizeDegrees;
}
