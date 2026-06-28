#include "UI/EMPLayerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Input/Reply.h"
#include "UI/EMPMarkWidget.h"

void UEMPLayerWidget::BindEMPComponent(UPartnerEMPComponent* InEMPComponent)
{
	EMPComponent = InEMPComponent;
}

void UEMPLayerWidget::SetMarkWidgetClass(TSubclassOf<UEMPMarkWidget> InMarkWidgetClass)
{
	MarkWidgetClass = InMarkWidgetClass;
}

void UEMPLayerWidget::InitializeMarkingTimer(float Duration)
{
	MarkingDuration = FMath::Max(Duration, 0.01f);
	ElapsedTime = 0.0f;
	bTimerActive = true;
	bFinished = false;

	if (EMPProgressBar)
	{
		EMPProgressBar->SetPercent(0.0f);
	}
}

void UEMPLayerWidget::OnTabPressed()
{
	if (bFinished || !EMPComponent)
	{
		return;
	}

	if (!EMPComponent->HasMarkedTargets())
	{
		// 마킹된 대상 없음 — Tab 무시
		return;
	}

	FinishEMP();
}

void UEMPLayerWidget::FinishEMP()
{
	if (bFinished || !EMPComponent)
	{
		return;
	}

	if (!EMPComponent->HasMarkedTargets())
	{
		return;
	}

	if (!EMPComponent->NotifyEMPConfirmed())
	{
		return;
	}

	bFinished = true;
	bTimerActive = false;
}

void UEMPLayerWidget::ExpireEMP()
{
	if (bFinished || !EMPComponent)
	{
		return;
	}

	bFinished = true;
	bTimerActive = false;
	EMPComponent->NotifyEMPExpired();
}

void UEMPLayerWidget::ClearMarkers()
{
	for (const TPair<TObjectPtr<AActor>, TObjectPtr<UEMPMarkWidget>>& Pair : MarkWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromParent();
		}
	}

	MarkWidgets.Reset();

	if (MarkerCanvas)
	{
		MarkerCanvas->ClearChildren();
	}
}

void UEMPLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetIsFocusable(true);
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

	// BP에서 BindWidget으로 연결된 게 없을 때 코드 fallback
	if (!EMPProgressBar && WidgetTree)
	{
		EMPProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("EMPProgressBar"));
	}

	if (EMPProgressBar)
	{
		EMPProgressBar->SetPercent(0.0f);
	}
}

FReply UEMPLayerWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		OnTabPressed();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UEMPLayerWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		OnTabPressed();
		return FReply::Handled();
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UEMPLayerWidget::NativeDestruct()
{
	BindEMPComponent(nullptr);
	ClearMarkers();

	Super::NativeDestruct();
}

void UEMPLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 타이머 진행
	if (bTimerActive && !bFinished)
	{
		ElapsedTime += InDeltaTime;
		const float Progress = FMath::Clamp(ElapsedTime / MarkingDuration, 0.0f, 1.0f);

		if (EMPProgressBar)
		{
			EMPProgressBar->SetPercent(Progress);
		}

		if (ElapsedTime >= MarkingDuration)
		{
			ExpireEMP();
		}
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	for (auto It = MarkWidgets.CreateIterator(); It; ++It)
	{
		AActor* TargetActor = It.Key();
		UEMPMarkWidget* Mark = It.Value();

		if (!TargetActor || !Mark)
		{
			It.RemoveCurrent();
			continue;
		}

		FVector2D ScreenLocation = FVector2D::ZeroVector;
		if (!PC->ProjectWorldLocationToScreen(TargetActor->GetActorLocation(), ScreenLocation, true))
		{
			Mark->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		Mark->SetVisibility(ESlateVisibility::Visible);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Mark->Slot))
		{
			const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
			CanvasSlot->SetPosition(ScreenLocation / ViewportScale);
		}
	}
}

void UEMPLayerWidget::AddCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent)
{
	if (!MarkerCanvas || !TargetActor || MarkWidgets.Contains(TargetActor))
	{
		return;
	}

	TSubclassOf<UEMPMarkWidget> EffectiveClass = MarkWidgetClass;
	if (!EffectiveClass)
	{
		EffectiveClass = UEMPMarkWidget::StaticClass();
	}

	UEMPMarkWidget* Mark = CreateWidget<UEMPMarkWidget>(GetOwningPlayer(), EffectiveClass);
	if (!Mark)
	{
		return;
	}

	Mark->InitializeMark(TargetActor, EMPableComponent, EMPComponent);
	Mark->SetVisibility(ESlateVisibility::Visible);

	UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(Mark);
	if (CanvasSlot)
	{
		// 실패
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetSize(MarkerSize);
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	}

	MarkWidgets.Add(TargetActor, Mark);
}

void UEMPLayerWidget::RemoveCandidate(AActor* TargetActor, UEMPableComponent* EMPableComponent)
{
	if (TObjectPtr<UEMPMarkWidget>* Mark = MarkWidgets.Find(TargetActor))
	{
		if (*Mark)
		{
			(*Mark)->RemoveFromParent();
		}

		MarkWidgets.Remove(TargetActor);
	}
}
