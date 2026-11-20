#include "UI/TargetPointHudWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"

float FTargetPointHudSettings::GetDistanceScale(float Distance) const
{
	const float Lower = FMath::Max(0.01f, MinScale);
	return FMath::Clamp(FMath::Max(1.0f, ReferenceDistance) / FMath::Max(1.0f, Distance),
		Lower, FMath::Max(Lower, MaxScale));
}

namespace
{
constexpr double RearIndicatorAngle = 15.0;
constexpr double RearCenterAngle = 2.0;
constexpr double RearTransitionStartAngle = 60.0;

FBox2D GetAnchorBounds(const FTargetPointHudSettings& Settings, FVector2D ViewportSize, FVector2D ContentSize,
	float Distance, FVector2D Pivot, FVector2D& OutPivotOffset, float& OutScale)
{
	ViewportSize = ViewportSize.ComponentMax(FVector2D(1.0));
	ContentSize = ContentSize.ComponentMax(FVector2D(1.0));
	const FVector2D Padding(FMath::Clamp(double(Settings.ScreenPadding), 0.0, (ViewportSize.X - 1.0) * 0.5),
		FMath::Clamp(double(Settings.ScreenPadding), 0.0, (ViewportSize.Y - 1.0) * 0.5));
	const FVector2D Available = ViewportSize - Padding * 2.0;
	// 작은 화면에서는 거리 배율보다 위젯 전체가 여백 안에 들어가는 것을 우선한다.
	OutScale = FMath::Min(Settings.GetDistanceScale(Distance),
		float(FMath::Min(Available.X / ContentSize.X, Available.Y / ContentSize.Y)));
	const FVector2D Size = ContentSize * OutScale;
	OutPivotOffset = Size * Pivot.ClampAxes(0.0, 1.0);
	const FVector2D Min = Padding + OutPivotOffset;
	return FBox2D(Min, (ViewportSize - Padding - (Size - OutPivotOffset)).ComponentMax(Min));
}

FVector2D ClampAnchor(FVector2D Point, const FBox2D& Bounds)
{
	return Point.ComponentMax(Bounds.Min).ComponentMin(Bounds.Max);
}

FVector2D IntersectEdge(FVector2D Origin, FVector2D Direction, const FBox2D& Bounds)
{
	const FVector2D UnitDirection = Direction.GetSafeNormal();
	const double Horizontal = FMath::Abs(UnitDirection.X) > UE_SMALL_NUMBER
		? ((UnitDirection.X > 0.0 ? Bounds.Max.X : Bounds.Min.X) - Origin.X) / UnitDirection.X
		: TNumericLimits<double>::Max();
	const double Vertical = FMath::Abs(UnitDirection.Y) > UE_SMALL_NUMBER
		? ((UnitDirection.Y > 0.0 ? Bounds.Max.Y : Bounds.Min.Y) - Origin.Y) / UnitDirection.Y
		: TNumericLimits<double>::Max();
	return ClampAnchor(Origin + UnitDirection * FMath::Min(Horizontal, Vertical), Bounds);
}

FVector2D MoveAlongEdgeToBottom(FVector2D Start, FVector2D Origin, const FBox2D& Bounds, double Progress)
{
	const FVector2D BottomCenter(Origin.X, Bounds.Max.Y);
	if (Progress >= 1.0)
	{
		return BottomCenter;
	}
	if (FMath::IsNearlyEqual(Start.Y, BottomCenter.Y))
	{
		return FVector2D(FMath::Lerp(Start.X, BottomCenter.X, Progress), BottomCenter.Y);
	}

	// 상단에서 출발하면 먼저 같은 쪽 모서리로 이동한다. 이후 옆면을 내려가 하단 중앙에 도착한다.
	const double SideX = Start.X <= Origin.X ? Bounds.Min.X : Bounds.Max.X;
	const double ToSide = FMath::Abs(SideX - Start.X);
	const double ToBottom = Bounds.Max.Y - Start.Y;
	const double ToCenter = FMath::Abs(BottomCenter.X - SideX);
	const double Travel = (ToSide + ToBottom + ToCenter) * Progress;
	if (Travel < ToSide)
	{
		return FVector2D(FMath::Lerp(Start.X, SideX, Travel / ToSide), Start.Y);
	}
	if (Travel < ToSide + ToBottom)
	{
		return FVector2D(SideX, Start.Y + Travel - ToSide);
	}
	return FVector2D(FMath::Clamp(SideX + FMath::Sign(BottomCenter.X - SideX) * (Travel - ToSide - ToBottom),
		Bounds.Min.X, Bounds.Max.X), BottomCenter.Y);
}
}

void FTargetPointHudSettings::CalculateLayout(FVector2D ScreenPoint, FVector2D ViewportSize,
	FVector2D ContentSize, float Distance, FVector2D& OutPosition, float& OutScale, FVector2D Pivot) const
{
	FVector2D PivotOffset;
	const FBox2D Bounds = GetAnchorBounds(*this, ViewportSize, ContentSize, Distance, Pivot, PivotOffset, OutScale);
	OutPosition = ClampAnchor(ScreenPoint, Bounds) - PivotOffset;
}

bool FTargetPointHudSettings::ResolveWorldLayout(bool bProjected, FVector2D ScreenPoint,
	FVector2D RearDirection, double RearAngle, double RearDepth, FVector2D ViewportSize, FVector2D ContentSize, float Distance,
	FVector2D Pivot, FVector2D& OutPosition, float& OutScale, bool* bOutShowRearIndicator) const
{
	if (bOutShowRearIndicator)
	{
		*bOutShowRearIndicator = !bProjected && bKeepBehindCamera && RearAngle <= RearIndicatorAngle;
	}
	if (!bProjected && !bKeepBehindCamera)
	{
		return false;
	}

	FVector2D PivotOffset;
	const FBox2D Bounds = GetAnchorBounds(*this, ViewportSize, ContentSize, Distance, Pivot, PivotOffset, OutScale);
	if (bProjected && ScreenPoint == ClampAnchor(ScreenPoint, Bounds))
	{
		OutPosition = ScreenPoint - PivotOffset;
		return true;
	}

	// 피벗과 크기가 달라도 HUD의 화면 중심에서 같은 방향을 유지한다.
	// 아주 작은 화면에서는 중심이 앵커 범위 밖에 있을 수 있어 시작점을 범위 안에 둔다.
	const FVector2D ScreenCenter = ViewportSize * 0.5;
	const FVector2D Origin = ClampAnchor(ScreenCenter, Bounds);
	FVector2D Direction = bProjected ? ScreenPoint - Origin : RearDirection;
	if (Direction.IsNearlyZero())
	{
		Direction = FVector2D(0.0, 1.0);
	}
	FVector2D Anchor = IntersectEdge(Origin, Direction, Bounds);
	if (!bProjected && RearAngle < RearTransitionStartAngle)
	{
		const double DepthBlend = FMath::SmoothStep(0.0, 0.1, RearDepth);
		const double Progress = (1.0 - FMath::SmoothStep(RearCenterAngle, RearTransitionStartAngle, RearAngle)) * DepthBlend;
		Anchor = MoveAlongEdgeToBottom(Anchor, Origin, Bounds, Progress);
	}
	OutPosition = Anchor - PivotOffset;
	return true;
}

bool FTargetPointHudSettings::Project(APlayerController* Controller, const FVector& WorldPoint,
	FVector2D ContentSize, FVector2D& OutPosition, float& OutScale, FVector2D Pivot, bool* bOutShowRearIndicator) const
{
	if (bOutShowRearIndicator)
	{
		*bOutShowRearIndicator = false;
	}
	if (!Controller || !Controller->IsLocalController())
	{
		return false;
	}
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(Controller).GetLocalSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	FVector2D ScreenPoint;
	const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, WorldPoint, ScreenPoint, true);
	if (!bProjected && !bKeepBehindCamera)
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector WorldDirection = WorldPoint - CameraLocation;
	FVector2D RearDirection = FVector2D::ZeroVector;
	double RearAngle = 180.0;
	double RearDepth = 0.0;
	if (!bProjected)
	{
		const FVector LocalDirection = CameraRotation.UnrotateVector(WorldDirection);
		RearDepth = -LocalDirection.GetSafeNormal().X;
		const FRotator CameraYaw(0.0, CameraRotation.Yaw, 0.0);
		const double Forward = FVector::DotProduct(WorldDirection.GetSafeNormal2D(), CameraYaw.Vector());
		RearAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(-Forward, -1.0, 1.0)));

		FVector EdgeDirection = WorldDirection;
		if (RearAngle < RearTransitionStartAngle)
		{
			// 후방 구간에서는 현재 시점의 피치와 높이를 유지하고, 전환 진입 각도의 투영을 출발점으로 쓴다.
			// 카메라 평면에 가까울수록 일반 투영으로 이어지도록 출발점 보정을 줄인다.
			const double DepthBlend = FMath::SmoothStep(0.0, 0.1, RearDepth);
			const FVector LocalYawDirection = CameraYaw.UnrotateVector(WorldDirection);
			const double Side = LocalYawDirection.Y < 0.0 ? -1.0 : 1.0;
			const double EntryYawOffset = Side * (RearAngle - RearTransitionStartAngle) * DepthBlend;
			EdgeDirection = WorldDirection.RotateAngleAxis(EntryYawOffset, FVector::UpVector);
		}

		const ULocalPlayer* Player = Controller->GetLocalPlayer();
		FSceneViewProjectionData ProjectionData;
		if (Player && Player->ViewportClient && Player->GetProjectionData(Player->ViewportClient->Viewport, ProjectionData))
		{
			// 깊이로 나누기 전의 투영 벡터를 사용한다. W가 0을 지나도 화면 방향이 뒤집히지 않는다.
			const FVector4 Clip = ProjectionData.ComputeViewProjectionMatrix().TransformFVector4(
				FVector4(CameraLocation + EdgeDirection, 1.0));
			const FIntRect& ViewRect = ProjectionData.GetConstrainedViewRect();
			RearDirection = FVector2D(Clip.X * ViewRect.Width(), -Clip.Y * ViewRect.Height());
		}
		else
		{
			const FVector LocalEdgeDirection = CameraRotation.UnrotateVector(EdgeDirection);
			RearDirection = FVector2D(LocalEdgeDirection.Y, -LocalEdgeDirection.Z);
		}
	}

	return ResolveWorldLayout(bProjected, ScreenPoint, RearDirection, RearAngle, RearDepth, ViewportSize, ContentSize,
		WorldDirection.Size(), Pivot, OutPosition, OutScale, bOutShowRearIndicator);
}

UTargetPointHudWidget* UTargetPointHudWidget::CreateForPlayer(APlayerController* Controller, UObject* RequestOwner)
{
	if (!Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer()) return nullptr;
	ULocalPlayerUILayerSubsystem* Layers = Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerUILayerSubsystem>();
	if (!Layers) return nullptr;
	UTargetPointHudWidget* Hud = CreateWidget<UTargetPointHudWidget>(Controller, StaticClass());
	if (!Hud) return nullptr;
	Hud->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	// 유효한 입력 모드 태그는 레이어 등록에 필요하지만, 표시 전용이므로 입력 소유권은 요청하지 않는다.
	Hud->LayerHandle = Layers->PushWidget(UILayerTags::Gameplay(), Hud, FirstPersonInputModeTags::UI(), RequestOwner,
		EUILayerFocusTarget::None, false, false);
	return Hud->LayerHandle.IsValid() ? Hud : nullptr;
}

void UTargetPointHudWidget::Release()
{
	if (ULocalPlayer* Player = GetOwningLocalPlayer())
	{
		if (ULocalPlayerUILayerSubsystem* Layers = Player->GetSubsystem<ULocalPlayerUILayerSubsystem>())
		{
			Layers->PopLayer(LayerHandle);
		}
	}
	LayerHandle.Reset();
	RemoveFromParent();
}

void UTargetPointHudWidget::AddContent(UUserWidget* Content)
{
	if (!Content || !ContentCanvas) return;
	if (Content->GetParent() != ContentCanvas)
	{
		ContentCanvas->AddChildToCanvas(Content);
		Content->SetRenderTransformPivot(FVector2D::ZeroVector);
		Content->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTargetPointHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ContentCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	ContentCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = ContentCanvas;
}

void UTargetPointHudWidget::UpdateContent(UTargetPointWidget* Content, bool bShown, const FVector& WorldPoint,
	FVector2D FallbackSize)
{
	if (!Content) return;
	Content->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShown) return;
	Content->ForceLayoutPrepass();
	FVector2D Size = Content->GetDesiredSize();
	if (Size.X <= 0) Size.X = FMath::Max(1.0, FallbackSize.X);
	if (Size.Y <= 0) Size.Y = FMath::Max(1.0, FallbackSize.Y);
	UpdateContent(Content, true, WorldPoint, Size, Content->HudPresentation, FVector2D(0.5));
}

void UTargetPointHudWidget::UpdateContent(UUserWidget* Content, bool bShown, const FVector& WorldPoint,
	FVector2D Size, const FTargetPointHudSettings& Settings, FVector2D Pivot, bool* bOutShowRearIndicator)
{
	if (bOutShowRearIndicator)
	{
		*bOutShowRearIndicator = false;
	}
	if (!Content)
	{
		return;
	}
	if (!bShown)
	{
		Content->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// HUD 교체로 레이어 루트가 제거되었더라도, 다음 표시 요청에서 기존 내용을 다시 연결한다.
	if (!GetParent())
	{
		ULocalPlayer* Player = GetOwningLocalPlayer();
		ULocalPlayerUILayerSubsystem* Layers = Player ? Player->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
		LayerHandle = Layers ? Layers->PushWidget(UILayerTags::Gameplay(), this, FirstPersonInputModeTags::UI(), this,
			EUILayerFocusTarget::None, false, false) : FUILayerHandle();
		if (!LayerHandle.IsValid())
		{
			Content->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
	}
	Content->SetVisibility(ESlateVisibility::HitTestInvisible);
	FVector2D Position;
	float Scale;
	if (!Settings.Project(GetOwningPlayer(), WorldPoint, Size, Position, Scale, Pivot, bOutShowRearIndicator))
	{
		Content->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Content->Slot))
	{
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetSize(Size);
		CanvasSlot->SetPosition(Position);
	}
	Content->SetRenderScale(FVector2D(Scale));
}
