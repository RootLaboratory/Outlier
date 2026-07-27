#include "UI/HackCandidateMarkerWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
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
	CancelHackHold();

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
	ShowHackableInfoWidget();

	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackMarkerHovered(this, TargetActor);
	}
}

void UHackCandidateMarkerWidget::HandleUnhovered()
{
	CancelHackHold();

	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackMarkerUnhovered(this, TargetActor);
	}

	HideHackableInfoWidget();
}

void UHackCandidateMarkerWidget::SetHackHoldProgress(float InProgress)
{
	EnsureHackableInfoWidget();

	if (HackableInfoWidget)
	{
		HackableInfoWidget->SetHackProgress(InProgress);
	}
}

void UHackCandidateMarkerWidget::StartHackHold(float InHoldDuration)
{
	CancelHackHold();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	HackHoldDuration = FMath::Max(InHoldDuration, KINDA_SMALL_NUMBER);
	HackHoldStartTime = World->GetTimeSeconds();
	bIsHoldingHack = true;
	SetHackHoldProgress(0.0f);

	FTimerManagerTimerParameters TimerParameters;
	TimerParameters.bLoop = true;
	TimerParameters.bMaxOncePerFrame = true;

	World->GetTimerManager().SetTimer(
		HackHoldTimerHandle,
		this,
		&UHackCandidateMarkerWidget::TickHackHold,
		1.0f / 60.0f,
		TimerParameters);
}

void UHackCandidateMarkerWidget::CancelHackHold()
{
	bIsHoldingHack = false;
	HackHoldStartTime = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HackHoldTimerHandle);
	}
	else
	{
		HackHoldTimerHandle.Invalidate();
	}

	if (HackableInfoWidget)
	{
		HackableInfoWidget->SetHackProgress(0.0f);
	}
}

void UHackCandidateMarkerWidget::TickHackHold()
{
	UWorld* World = GetWorld();
	if (!bIsHoldingHack || !World)
	{
		CancelHackHold();
		return;
	}

	const float HackHoldElapsed = World->GetTimeSeconds() - HackHoldStartTime;
	const float HoldProgress = FMath::Clamp(HackHoldElapsed* UpdateScale / HackHoldDuration, 0.0f, 1.0f);
	SetHackHoldProgress(HoldProgress);

	if (HoldProgress < 1.0f)
	{
		return;
	}

	bIsHoldingHack = false;
	HackHoldStartTime = 0.0f;
	World->GetTimerManager().ClearTimer(HackHoldTimerHandle);

	if (HackComponent && TargetActor)
	{
		HackComponent->NotifyHackHoldCompleted(this, TargetActor);
	}
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
	HackableInfoWidget->ForceLayoutPrepass(); //Text에 따른 desired size 계산을 강제함.

	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	if (!CalculateInfoWidgetLayout(Position, Size))
	{
		HideHackableInfoWidget();
		return;
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(HackableInfoWidget->Slot))
	{
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
		CanvasSlot->SetPosition(Position);
		CanvasSlot->SetSize(Size);
		CanvasSlot->SetZOrder(10);
	}

	HackableInfoWidget->SetRenderScale(FVector2D::UnitVector);
	HackableInfoWidget->SetVisibility(PreviousVisibility);
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
}

void UHackCandidateMarkerWidget::HideHackableInfoWidget()
{
	if (HackableInfoWidget)
	{
		HackableInfoWidget->ResetPopup();
		HackableInfoWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UHackCandidateMarkerWidget::CalculateInfoWidgetLayout(FVector2D& OutPosition, FVector2D& OutSize) const
{
	if (!HackableInfoWidget || !TargetActor || !HackComponent)
	{
		return false;
	}

	AActor* PartnerActor = HackComponent->GetOwner();
	APlayerController* PC = GetOwningPlayer();
	if (!PartnerActor || !PC)
	{
		return false;
	}

	FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
	ViewportSize /= ViewportScale;

	FVector2D TargetScreenLocation = FVector2D::ZeroVector;
	if (!PC->ProjectWorldLocationToScreen(TargetActor->GetActorLocation(), TargetScreenLocation, true))
	{
		return false;
	}

	FVector2D TargetPosition = TargetScreenLocation / ViewportScale;
	FVector2D PartnerPosition = ViewportSize * 0.5f;
	FVector2D PartnerScreenLocation = FVector2D::ZeroVector;
	if (PC->ProjectWorldLocationToScreen(PartnerActor->GetActorLocation(), PartnerScreenLocation, true))
	{
		PartnerPosition = PartnerScreenLocation / ViewportScale;
	}

	const FVector2D ViewportPadding = HackableInfoWidget->GetViewportPadding();
	const FVector2D InnerViewportSize(
		FMath::Max(ViewportSize.X - ViewportPadding.X * 2.0f, 1.0f),
		FMath::Max(ViewportSize.Y - ViewportPadding.Y * 2.0f, 1.0f));

	const float ActorDistance = FVector::Distance(PartnerActor->GetActorLocation(), TargetActor->GetActorLocation());
	const float WidgetWidth = FMath::Max(HackableInfoWidget->GetWidgetWidth(), 1.0f);
	const float MinWidgetHeight = FMath::Max(HackableInfoWidget->GetMinWidgetHeight(), 1.0f);
	const float MaxWidgetHeight = FMath::Max(HackableInfoWidget->GetMaxWidgetHeight(), MinWidgetHeight);
	const FVector2D DesiredSize = HackableInfoWidget->GetDesiredSize();

	OutSize.X = FMath::Min(WidgetWidth, InnerViewportSize.X);
	OutSize.Y = FMath::Clamp(DesiredSize.Y, MinWidgetHeight, MaxWidgetHeight);
	OutSize.Y = FMath::Min(OutSize.Y, InnerViewportSize.Y);

	const FVector2D PlacementDirection = (TargetPosition - PartnerPosition).GetSafeNormal();
	const int32 PreferredXSign = PlacementDirection.X >= 0.0f ? -1 : 1;
	const int32 PreferredYSign = PlacementDirection.Y >= 0.0f ? 1 : -1;
	const FVector2D InfoOffset = HackableInfoWidget->GetOffsetForDistance(ActorDistance);

	const auto BuildPosition = [&TargetPosition, &InfoOffset, &OutSize](int32 XSign, int32 YSign)
	{
		FVector2D CandidatePosition = TargetPosition;
		CandidatePosition.X += XSign > 0 ? InfoOffset.X : -InfoOffset.X - OutSize.X;
		CandidatePosition.Y += YSign > 0 ? InfoOffset.Y : -InfoOffset.Y - OutSize.Y;
		return CandidatePosition;
	};

	const auto GetOverflow = [&ViewportSize, &ViewportPadding, &OutSize](const FVector2D& Position)
	{
		float Overflow = 0.0f;
		Overflow += FMath::Max(ViewportPadding.X - Position.X, 0.0f);
		Overflow += FMath::Max(ViewportPadding.Y - Position.Y, 0.0f);
		Overflow += FMath::Max(Position.X + OutSize.X - (ViewportSize.X - ViewportPadding.X), 0.0f);
		Overflow += FMath::Max(Position.Y + OutSize.Y - (ViewportSize.Y - ViewportPadding.Y), 0.0f);
		return Overflow;
	};

	const int32 CandidateSigns[4][2] =
	{
		{ PreferredXSign, PreferredYSign },
		{ PreferredXSign, -PreferredYSign },
		{ -PreferredXSign, PreferredYSign },
		{ -PreferredXSign, -PreferredYSign }
	};

	FVector2D BestPosition = BuildPosition(PreferredXSign, PreferredYSign);
	float BestOverflow = GetOverflow(BestPosition);
	for (const int32(&CandidateSign)[2] : CandidateSigns)
	{
		const FVector2D CandidatePosition = BuildPosition(CandidateSign[0], CandidateSign[1]);
		const float CandidateOverflow = GetOverflow(CandidatePosition);
		if (CandidateOverflow <= KINDA_SMALL_NUMBER)
		{
			OutPosition = CandidatePosition;
			return true;
		}

		if (CandidateOverflow < BestOverflow)
		{
			BestOverflow = CandidateOverflow;
			BestPosition = CandidatePosition;
		}
	}

	OutPosition.X = FMath::Clamp(BestPosition.X, ViewportPadding.X, ViewportSize.X - ViewportPadding.X - OutSize.X);
	OutPosition.Y = FMath::Clamp(BestPosition.Y, ViewportPadding.Y, ViewportSize.Y - ViewportPadding.Y - OutSize.Y);
	return true;
}
