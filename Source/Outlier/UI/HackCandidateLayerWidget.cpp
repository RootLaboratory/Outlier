#include "UI/HackCandidateLayerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/PartnerHackComponent.h"
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
}

void UHackCandidateLayerWidget::NativeDestruct()
{
	BindHackComponent(nullptr);
	ClearMarkers();

	Super::NativeDestruct();
}

void UHackCandidateLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	for (auto It = MarkerWidgets.CreateIterator(); It; ++It)
	{
		AActor* TargetActor = It.Key();
		UHackCandidateMarkerWidget* Marker = It.Value();

		if (!TargetActor || !Marker)
		{
			It.RemoveCurrent();
			continue;
		}

		FVector2D ScreenLocation = FVector2D::ZeroVector;
		if (!PC->ProjectWorldLocationToScreen(TargetActor->GetActorLocation(), ScreenLocation, true))
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
	}
}
