#include "UI/TargetPointHudWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"

float FTargetPointHudSettings::GetDistanceScale(float Distance) const
{
	const float Lower = FMath::Max(0.01f, MinScale);
	return FMath::Clamp(FMath::Max(1.0f, ReferenceDistance) / FMath::Max(1.0f, Distance),
		Lower, FMath::Max(Lower, MaxScale));
}

void FTargetPointHudSettings::CalculateLayout(FVector2D ScreenPoint, FVector2D ViewportSize,
	FVector2D ContentSize, float Distance, FVector2D& OutPosition, float& OutScale, FVector2D Pivot) const
{
	ViewportSize.X = FMath::Max(1.0, ViewportSize.X);
	ViewportSize.Y = FMath::Max(1.0, ViewportSize.Y);
	ContentSize.X = FMath::Max(1.0, ContentSize.X);
	ContentSize.Y = FMath::Max(1.0, ContentSize.Y);
	const FVector2D Padding(FMath::Clamp(double(ScreenPadding), 0.0, (ViewportSize.X - 1.0) * 0.5),
		FMath::Clamp(double(ScreenPadding), 0.0, (ViewportSize.Y - 1.0) * 0.5));
	const FVector2D Available = ViewportSize - Padding * 2.0;
	// 작은 화면에서도 UI 전체가 보이도록 거리 배율보다 화면 안으로의 배치를 우선한다.
	OutScale = FMath::Min(GetDistanceScale(Distance),
		float(FMath::Min(Available.X / ContentSize.X, Available.Y / ContentSize.Y)));
	const FVector2D Size = ContentSize * OutScale;
	OutPosition.X = FMath::Clamp(ScreenPoint.X - Size.X * FMath::Clamp(Pivot.X, 0.0, 1.0), Padding.X, ViewportSize.X - Padding.X - Size.X);
	OutPosition.Y = FMath::Clamp(ScreenPoint.Y - Size.Y * FMath::Clamp(Pivot.Y, 0.0, 1.0), Padding.Y, ViewportSize.Y - Padding.Y - Size.Y);
}

FVector2D FTargetPointHudSettings::GetScreenEdgePoint(const FVector& CameraLocalDirection, FVector2D ViewportSize)
{
	FVector2D Direction(CameraLocalDirection.Y, -CameraLocalDirection.Z);
	// 정후방은 화면 방향을 정할 수 없으므로 아래쪽 가장자리를 사용한다.
	if (Direction.IsNearlyZero()) Direction = FVector2D(0.0, 1.0);
	const FVector2D HalfSize = ViewportSize * 0.5;
	const double Horizontal = FMath::Abs(Direction.X) > UE_SMALL_NUMBER
		? HalfSize.X / FMath::Abs(Direction.X) : TNumericLimits<double>::Max();
	const double Vertical = FMath::Abs(Direction.Y) > UE_SMALL_NUMBER
		? HalfSize.Y / FMath::Abs(Direction.Y) : TNumericLimits<double>::Max();
	return HalfSize + Direction * FMath::Min(Horizontal, Vertical);
}

bool FTargetPointHudSettings::Project(APlayerController* Controller, const FVector& WorldPoint,
	FVector2D ContentSize, FVector2D& OutPosition, float& OutScale, FVector2D Pivot) const
{
	if (!Controller || !Controller->IsLocalController()) return false;
	FVector2D ScreenPoint;
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(Controller).GetLocalSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0) return false;
	FVector CameraLocation;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, WorldPoint, ScreenPoint, true))
	{
		if (!bKeepBehindCamera) return false;
		ScreenPoint = GetScreenEdgePoint(CameraRotation.UnrotateVector(WorldPoint - CameraLocation), ViewportSize);
	}
	CalculateLayout(ScreenPoint, ViewportSize, ContentSize, FVector::Distance(CameraLocation, WorldPoint), OutPosition, OutScale, Pivot);
	return true;
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
	FVector2D Size, const FTargetPointHudSettings& Settings, FVector2D Pivot)
{
	if (!Content) return;
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
	// 후방 표시가 허용된 마커는 투영 실패 시 화면 가장자리 좌표를 사용한다.
	if (!Settings.Project(GetOwningPlayer(), WorldPoint, Size, Position, Scale, Pivot))
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
