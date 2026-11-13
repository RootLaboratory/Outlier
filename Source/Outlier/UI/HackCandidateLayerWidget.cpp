#include "UI/HackCandidateLayerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interface/HackableInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/HackCandidateMarkerWidget.h"
#include "UI/HackableInfoWidget.h"

void UHackCandidateLayerWidget::BeginHackHold(AActor* TargetActor, UHackableInfoWidget* InfoWidget)
{
	EndHackHold();
	HeldActor = TargetActor;
	HeldInfoWidget = InfoWidget;
	if (HeldInfoWidget)
	{
		HeldInfoWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		HeldInfoWidget->SetHackProgress(0.0f);
	}
	UpdateHackHoldLayout();
	UpdateCandidateNullVisibility();
}

void UHackCandidateLayerWidget::EndHackHold()
{
	if (HeldInfoWidget)
	{
		HeldInfoWidget->SetHackProgress(0.0f);
		HeldInfoWidget->ResetPopup();
		HeldInfoWidget->RemoveFromParent();
	}
	HeldInfoWidget = nullptr;
	HeldActor = nullptr;
	UpdateCandidateNullVisibility();
}

void UHackCandidateLayerWidget::SetHackHoldProgress(float Progress)
{
	if (HeldInfoWidget) HeldInfoWidget->SetHackProgress(Progress);
}

void UHackCandidateLayerWidget::UpdateHackHoldLayout()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC || !IsValid(HeldActor) || !HeldInfoWidget) return;
	const FVector2D ViewSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC).GetLocalSize();
	if (ViewSize.X <= 0 || ViewSize.Y <= 0) return;
	const FVector Target = IHackableInterface::ResolveHackTargetLocation(HeldActor);
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector2D ScreenPoint;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Target, ScreenPoint, true))
	{
		// Projection fails behind the camera. Keep the info on the corresponding screen edge.
		const FVector LocalDirection = ViewRotation.UnrotateVector(Target - ViewLocation);
		FVector2D EdgeDirection(LocalDirection.Y, -LocalDirection.Z);
		if (!EdgeDirection.Normalize()) EdgeDirection = FVector2D(0.0, 1.0);
		ScreenPoint = ViewSize * 0.5 + EdgeDirection * (ViewSize.X + ViewSize.Y);
	}
	FVector2D Size = HeldInfoWidget->GetDesiredSize();
	Size.X = FMath::Max(Size.X, double(HeldInfoWidget->GetWidgetWidth()));
	Size.Y = FMath::Max(Size.Y, double(HeldInfoWidget->GetMinWidgetHeight()));
	FVector2D Position;
	float Scale;
	HudPresentation.CalculateLayout(ScreenPoint, ViewSize, Size,
		FVector::Distance(ViewLocation, Target), Position, Scale);
	if (UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(HeldInfoWidget->Slot))
	{
		InfoSlot->SetAutoSize(false);
		InfoSlot->SetAlignment(FVector2D::ZeroVector);
		InfoSlot->SetPosition(Position);
		InfoSlot->SetSize(Size);
		InfoSlot->SetZOrder(10);
	}
	HeldInfoWidget->SetRenderTransformPivot(FVector2D::ZeroVector);
	HeldInfoWidget->SetRenderScale(FVector2D(Scale));
}

void UHackCandidateLayerWidget::BindHackComponent(UPartnerHackComponent* InHackComponent)
{
	HackComponent = InHackComponent;
}

void UHackCandidateLayerWidget::SetMarkerWidgetClass(TSubclassOf<UHackCandidateMarkerWidget> InMarkerWidgetClass)
{
	MarkerWidgetClass = InMarkerWidgetClass;
}

void UHackCandidateLayerWidget::SetHackableInfoWidgetClass(TSubclassOf<UHackableInfoWidget> InHackableInfoWidgetClass)
{
	HackableInfoWidgetClass = InHackableInfoWidgetClass;
}

void UHackCandidateLayerWidget::ClearMarkers()
{
	for (const TPair<TObjectPtr<AActor>, TObjectPtr<UHackCandidateMarkerWidget>>& Pair : MarkerWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromParent();
		}
	}

	MarkerWidgets.Reset();
	UpdateCandidateNullVisibility();
}

void UHackCandidateLayerWidget::UpdateCandidateNullVisibility()
{
	if (!HackCandidateNull)
	{
		return;
	}

	const ESlateVisibility NullVisibility = MarkerWidgets.IsEmpty() && !HeldInfoWidget
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (HackCandidateNull->GetVisibility() != NullVisibility)
	{
		HackCandidateNull->SetVisibility(NullVisibility);
	}
}

void UHackCandidateLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (!MarkerCanvas && WidgetTree)
	{
		MarkerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MarkerCanvas"));
		WidgetTree->RootWidget = MarkerCanvas;
	}

	if (MarkerCanvas)
	{
		MarkerCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	StartReveal();
	UpdateCandidateNullVisibility();
}

void UHackCandidateLayerWidget::NativeDestruct()
{
	if (HackComponent) HackComponent->ResetLocalHackHoldProgress();
	EndHackHold();
	BindHackComponent(nullptr);
	ClearMarkers();
	bRevealActive = false;

	Super::NativeDestruct();
}

void UHackCandidateLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateReveal(InDeltaTime);
	UpdateHackHoldLayout();

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	bool bCandidatesRemoved = false;
	for (auto It = MarkerWidgets.CreateIterator(); It; ++It)
	{
		AActor* TargetActor = It.Key();
		UHackCandidateMarkerWidget* Marker = It.Value();

		if (!IsValid(TargetActor) || !IsValid(Marker))
		{
			if (IsValid(Marker))
			{
				Marker->RemoveFromParent();
			}
			It.RemoveCurrent();
			bCandidatesRemoved = true;
			continue;
		}

		FVector2D Position;
		float Scale;
		if (!HudPresentation.Project(PC, IHackableInterface::ResolveHackTargetLocation(TargetActor), MarkerSize, Position, Scale))
		{
			Marker->SetProjectionVisible(false);
			continue;
		}
		Marker->SetProjectionVisible(true);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
		{
			CanvasSlot->SetAlignment(FVector2D::ZeroVector);
			CanvasSlot->SetPosition(Position);
			CanvasSlot->SetSize(MarkerSize);
		}
		// 마커 전체에 같은 변환을 적용하므로 버튼의 히트 영역도 표시 크기를 따른다.
		Marker->SetRenderTransformPivot(FVector2D::ZeroVector);
		Marker->SetRenderScale(FVector2D(Scale));
		Marker->UpdateHudLayout(HudPresentation);

	}

	if (bCandidatesRemoved)
	{
		UpdateCandidateNullVisibility();
	}
}

void UHackCandidateLayerWidget::EnsureRevealMaterial()
{
	if (RevealMaterial || !HackingHUD)
	{
		return;
	}

	RevealMaterial = HackingHUD->GetDynamicMaterial();
}

void UHackCandidateLayerWidget::StartReveal()
{
	RevealElapsedTime = 0.0f;
	bRevealActive = RevealDuration > 0.0f;
	EnsureRevealMaterial();

	if (RevealMaterial)
	{
		RevealMaterial->SetScalarParameterValue(RevealRadiusParameterName,
			bRevealActive ? 0.0f : RevealMaxRadius);
	}
}

void UHackCandidateLayerWidget::UpdateReveal(float InDeltaTime)
{
	if (!bRevealActive)
	{
		return;
	}

	EnsureRevealMaterial();
	if (!RevealMaterial)
	{
		bRevealActive = false;
		return;
	}

	RevealElapsedTime += InDeltaTime;
	const float Duration = FMath::Max(RevealDuration, KINDA_SMALL_NUMBER);
	const float Progress = FMath::Clamp(RevealElapsedTime / Duration, 0.0f, 1.0f);
	const float EasedProgress = FMath::InterpEaseOut(0.0f, 1.0f, Progress, 2.0f);
	RevealMaterial->SetScalarParameterValue(RevealRadiusParameterName,
		FMath::Lerp(0.0f, RevealMaxRadius, EasedProgress));

	if (Progress >= 1.0f)
	{
		bRevealActive = false;
	}
}

void UHackCandidateLayerWidget::AddCandidate(AActor* TargetActor, UHackableComponent* HackableComponent)
{
	if (!MarkerCanvas || !TargetActor || MarkerWidgets.Contains(TargetActor))
	{
		return;
	}

	TSubclassOf<UHackCandidateMarkerWidget> EffectiveMarkerClass = MarkerWidgetClass;
	if (!EffectiveMarkerClass)
	{
		EffectiveMarkerClass = UHackCandidateMarkerWidget::StaticClass();
	}

	UHackCandidateMarkerWidget* Marker = CreateWidget<UHackCandidateMarkerWidget>(GetOwningPlayer(), EffectiveMarkerClass);
	if (!Marker)
	{
		return;
	}

	Marker->InitializeMarker(TargetActor, HackableComponent, HackComponent);
	Marker->SetHackableInfoWidgetClass(HackableInfoWidgetClass);
	Marker->SetVisibility(ESlateVisibility::Visible);

	UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(Marker);
	if (CanvasSlot)
	{
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetSize(MarkerSize);
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}

	Marker->EnsureHackableInfoWidget();
	MarkerWidgets.Add(TargetActor, Marker);
	UpdateCandidateNullVisibility();
}

void UHackCandidateLayerWidget::RemoveCandidate(AActor* TargetActor, UHackableComponent* HackableComponent)
{
	if (TObjectPtr<UHackCandidateMarkerWidget>* Marker = MarkerWidgets.Find(TargetActor))
	{
		if (*Marker)
		{
			(*Marker)->RemoveFromParent();
		}

		MarkerWidgets.Remove(TargetActor);
		UpdateCandidateNullVisibility();
	}
}
