#include "UI/HackCandidateMarkerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "UI/HackableInfoWidget.h"

void UHackCandidateMarkerWidget::InitializeMarker(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
	HackComponent = InHackComponent;
}

void UHackCandidateMarkerWidget::SetHackableInfoWidgetClass(TSubclassOf<UHackableInfoWidget> InHackableInfoWidgetClass)
{
	HackableInfoWidgetClass = InHackableInfoWidgetClass;
}

void UHackCandidateMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Visible);

	if (!SelectButton && WidgetTree)
	{
		SelectButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SelectButton"));
		WidgetTree->RootWidget = SelectButton;
	}

	if (SelectButton)
	{
		SelectButton->SetVisibility(ESlateVisibility::Visible);
		SelectButton->OnHovered.RemoveDynamic(this, &UHackCandidateMarkerWidget::HandleHovered);
		SelectButton->OnHovered.AddDynamic(this, &UHackCandidateMarkerWidget::HandleHovered);
		SelectButton->OnUnhovered.RemoveDynamic(this, &UHackCandidateMarkerWidget::HandleUnhovered);
		SelectButton->OnUnhovered.AddDynamic(this, &UHackCandidateMarkerWidget::HandleUnhovered);
	}
}

void UHackCandidateMarkerWidget::NativeDestruct()
{
	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackMarkerUnhovered(this, TargetActor);
	}

	if (HackableInfoWidget)
	{
		HackableInfoWidget->SetHackProgress(0.0f);
		HackableInfoWidget->RemoveFromParent();
		HackableInfoWidget = nullptr;
	}

	if (SelectButton)
	{
		SelectButton->OnHovered.RemoveDynamic(this, &UHackCandidateMarkerWidget::HandleHovered);
		SelectButton->OnUnhovered.RemoveDynamic(this, &UHackCandidateMarkerWidget::HandleUnhovered);
	}

	Super::NativeDestruct();
}
void UHackCandidateMarkerWidget::HandleHovered()
{
	if (HackComponent && HackComponent->IsHackHoldActive()) return;
	ShowHackableInfoWidget();

	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackMarkerHovered(this, TargetActor);
	}
}

void UHackCandidateMarkerWidget::HandleUnhovered()
{
	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackMarkerUnhovered(this, TargetActor);
	}

	HideHackableInfoWidget();
}

UHackableInfoWidget* UHackCandidateMarkerWidget::ReleaseInfoForHackHold()
{
	EnsureHackableInfoWidget();
	UHackableInfoWidget* Info = HackableInfoWidget;
	// Transfer ownership before removal so unhover/destruction cannot close the held info.
	HackableInfoWidget = nullptr;
	return Info;
}

float UHackCandidateMarkerWidget::GetHackHoldDuration(float BaseDuration) const
{
	return FMath::Max(BaseDuration, KINDA_SMALL_NUMBER) / FMath::Max(UpdateScale, KINDA_SMALL_NUMBER);
}

void UHackCandidateMarkerWidget::EnsureHackableInfoWidget()
{
	if (!TargetActor || !HackComponent)
	{
		return;
	}

	UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(GetParent());
	if (!ParentCanvas)
	{
		return;
	}

	const ESlateVisibility PreviousVisibility = HackableInfoWidget
		? HackableInfoWidget->GetVisibility()
		: ESlateVisibility::Collapsed;

	if (!HackableInfoWidget)
	{
		TSubclassOf<UHackableInfoWidget> EffectiveInfoClass = HackableInfoWidgetClass;
		if (!EffectiveInfoClass)
		{
			EffectiveInfoClass = UHackableInfoWidget::StaticClass();
		}

		HackableInfoWidget = CreateWidget<UHackableInfoWidget>(GetOwningPlayer(), EffectiveInfoClass);
		if (!HackableInfoWidget)
		{
			return;
		}

		HackableInfoWidget->InitializeInfo(TargetActor, HackableComponent);
	}

	if (!HackableInfoWidget->GetParent())
	{
		ParentCanvas->AddChildToCanvas(HackableInfoWidget);
	}

	HackableInfoWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	HackableInfoWidget->ForceLayoutPrepass();
	HackableInfoWidget->SetVisibility(PreviousVisibility);
	UpdateHudLayout(CurrentHudSettings);
}

void UHackCandidateMarkerWidget::ShowHackableInfoWidget()
{
	EnsureHackableInfoWidget();

	if (!HackableInfoWidget)
	{
		return;
	}

	HackableInfoWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	HackableInfoWidget->PlayPopup();
	UpdateHudLayout(CurrentHudSettings);
}

void UHackCandidateMarkerWidget::HideHackableInfoWidget()
{
	if (HackableInfoWidget)
	{
		HackableInfoWidget->ResetPopup();
		HackableInfoWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UHackCandidateMarkerWidget::SetProjectionVisible(bool bVisible)
{
	if (!bVisible && GetVisibility() != ESlateVisibility::Collapsed)
	{
		// 투영되지 않는 마커의 설명과 마우스 선택 상태가 화면에 남지 않도록 함께 정리한다.
		HandleUnhovered();
	}
	SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UHackCandidateMarkerWidget::UpdateHudLayout(const FTargetPointHudSettings& Settings)
{
	CurrentHudSettings = Settings;
	if (!HackableInfoWidget || !HackableInfoWidget->IsVisible()) return;
	const UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Slot);
	UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(HackableInfoWidget->Slot);
	if (!MarkerSlot || !InfoSlot) return;
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer()).GetLocalSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0) return;
	FVector2D Size = HackableInfoWidget->GetDesiredSize();
	Size.X = FMath::Max(Size.X, double(HackableInfoWidget->GetWidgetWidth()));
	Size.Y = FMath::Max(Size.Y, double(HackableInfoWidget->GetMinWidgetHeight()));
	const float MarkerScale = GetRenderTransform().Scale.X;
	const FVector2D MarkerPosition = MarkerSlot->GetPosition();
	const FVector2D MarkerSize = MarkerSlot->GetSize() * MarkerScale;
	// 거리별 위치 Offset 대신 마커 아래의 내부 여백만 둔다. 아래 공간이 부족하면 위로 배치한다.
	const float Gap = 8.0f * MarkerScale;
	FVector2D Center(MarkerPosition.X + MarkerSize.X * 0.5,
		MarkerPosition.Y + MarkerSize.Y + Gap + Size.Y * MarkerScale * 0.5);
	if (Center.Y + Size.Y * MarkerScale * 0.5 > ViewportSize.Y - Settings.ScreenPadding)
	{
		Center.Y = MarkerPosition.Y - Gap - Size.Y * MarkerScale * 0.5;
	}
	FTargetPointHudSettings LayoutSettings = Settings;
	LayoutSettings.MinScale = MarkerScale;
	LayoutSettings.MaxScale = MarkerScale;
	FVector2D Position;
	float InfoScale;
	LayoutSettings.CalculateLayout(Center, ViewportSize, Size, Settings.ReferenceDistance, Position, InfoScale);
	InfoSlot->SetAutoSize(false);
	InfoSlot->SetAlignment(FVector2D::ZeroVector);
	InfoSlot->SetPosition(Position);
	InfoSlot->SetSize(Size);
	InfoSlot->SetZOrder(10);
	HackableInfoWidget->SetRenderTransformPivot(FVector2D::ZeroVector);
	HackableInfoWidget->SetRenderScale(FVector2D(InfoScale));
}
