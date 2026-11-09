#include "UI/HackCandidateLayerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Interface/HackableInterface.h"
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
