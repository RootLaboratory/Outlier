#include "UI/HackCandidateMarkerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
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
		HackComponent->NotifyHackMarkerUnhovered(this, TargetActor, true);
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

	// 정보창 표시는 해킹 컴포넌트가 몸체·마커 hover를 합쳐 결정한다.
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
	else
	{
		HideHackableInfoWidget();
	}
}

UHackableInfoWidget* UHackCandidateMarkerWidget::ReleaseInfoForHackHold()
{
	EnsureHackableInfoWidget();
	UHackableInfoWidget* Info = HackableInfoWidget;
	// 마커가 제거돼도 홀드 정보창이 닫히지 않도록 먼저 참조를 넘긴다.
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
	const bool bWasVisible = GetVisibility() != ESlateVisibility::Collapsed;
	SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bVisible && bWasVisible)
	{
		// 투영되지 않는 마커의 설명과 마우스 선택 상태가 화면에 남지 않도록 함께 정리한다.
		// 접힌 뒤에 알려야 해킹 컴포넌트가 다시 판정할 때 이 마커를 고르지 않는다.
		HideHackableInfoWidget();
		HandleUnhovered();
	}
}

void UHackCandidateMarkerWidget::SetInfoShown(bool bShown)
{
	if (bShown)
	{
		ShowHackableInfoWidget();
	}
	else
	{
		HideHackableInfoWidget();
	}
}

void UHackCandidateMarkerWidget::UpdateHudLayout(const FTargetPointHudSettings& Settings)
{
	CurrentHudSettings = Settings;
	if (!HackableInfoWidget || !HackableInfoWidget->IsVisible())
	{
		return;
	}
	if (HackableInfoWidget->HasHudTracking())
	{
		HackableInfoWidget->UpdateHudTracking();
		return;
	}
	const UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Slot);
	APlayerController* PC = GetOwningPlayer();
	if (!MarkerSlot || !PC)
	{
		return;
	}
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC).GetLocalSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return;
	}
	const FVector Target = IHackableInterface::ResolveHackTargetLocation(TargetActor);
	const FVector2D MarkerDrawSize = MarkerSlot->GetSize();
	FTargetPointHudSettings PopupTrackingSettings = Settings;
	// 후보 마커의 설정과 무관하게 팝업은 전방 투영에 성공할 때만 표시한다.
	PopupTrackingSettings.bKeepBehindCamera = false;
	FVector2D MarkerPosition;
	float MarkerScale;
	if (!PopupTrackingSettings.Project(PC, Target, MarkerDrawSize, MarkerPosition, MarkerScale))
	{
		HackableInfoWidget->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	FVector2D Size = HackableInfoWidget->GetDesiredSize();
	Size.X = FMath::Max(Size.X, double(HackableInfoWidget->GetWidgetWidth()));
	Size.Y = FMath::Max(Size.Y, double(HackableInfoWidget->GetMinWidgetHeight()));
	const FVector2D MarkerSize = MarkerDrawSize * MarkerScale;
	// 최초 팝업만 화면 안에 배치한다. 이후에는 이때 정한 화면 오프셋을 유지한다.
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
	HackableInfoWidget->InitializeHudTracking(PopupTrackingSettings, MarkerDrawSize, MarkerPosition,
		Position, Size, InfoScale);
}
