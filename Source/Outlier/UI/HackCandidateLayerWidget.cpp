#include "UI/HackCandidateLayerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Interface/HackableInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/HackCandidateMarkerWidget.h"

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

	const ESlateVisibility NullVisibility = MarkerWidgets.IsEmpty()
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
	BindHackComponent(nullptr);
	ClearMarkers();
	bRevealActive = false;

	Super::NativeDestruct();
}

void UHackCandidateLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateReveal(InDeltaTime);

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

		FVector2D ScreenLocation = FVector2D::ZeroVector;
		if (!PC->ProjectWorldLocationToScreen(
			IHackableInterface::ResolveHackTargetLocation(TargetActor), ScreenLocation, true))
		{
			Marker->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		Marker->SetVisibility(ESlateVisibility::Visible);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
		{
			const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
			CanvasSlot->SetPosition(ScreenLocation / ViewportScale);
		}

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
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
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
