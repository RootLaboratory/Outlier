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
	FVector2D ContentSize, float Distance, FVector2D& OutPosition, float& OutScale) const
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
	OutPosition.X = FMath::Clamp(ScreenPoint.X - Size.X * 0.5, Padding.X, ViewportSize.X - Padding.X - Size.X);
	OutPosition.Y = FMath::Clamp(ScreenPoint.Y - Size.Y * 0.5, Padding.Y, ViewportSize.Y - Padding.Y - Size.Y);
}

bool FTargetPointHudSettings::Project(APlayerController* Controller, const FVector& WorldPoint,
	FVector2D ContentSize, FVector2D& OutPosition, float& OutScale) const
{
	if (!Controller || !Controller->IsLocalController()) return false;
	FVector2D ScreenPoint;
	// 플레이어별 뷰포트 좌표와 DPI를 같은 경로에서 변환한다. 카메라 뒤의 점은 표시하지 않는다.
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, WorldPoint, ScreenPoint, true)) return false;
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(Controller).GetLocalSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0) return false;
	FVector CameraLocation;
	FRotator CameraRotation;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
	CalculateLayout(ScreenPoint, ViewportSize, ContentSize, FVector::Distance(CameraLocation, WorldPoint), OutPosition, OutScale);
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
	Content->ForceLayoutPrepass();
	FVector2D Size = Content->GetDesiredSize();
	if (Size.X <= 0) Size.X = FMath::Max(1.0, FallbackSize.X);
	if (Size.Y <= 0) Size.Y = FMath::Max(1.0, FallbackSize.Y);
	FVector2D Position;
	float Scale;
	// 배율과 화면 여백은 표시할 위젯 BP의 기본값을 사용한다. 대상별 값은 전달받지 않는다.
	if (!Content->HudPresentation.Project(GetOwningPlayer(), WorldPoint, Size, Position, Scale))
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
