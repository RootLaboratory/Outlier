#include "UI/HackMiniGameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/ClickCircleMiniGameWidget.h"
#include "UI/HackingMiniGameBase.h"
#include "UI/SpinningCircleMiniGameWidget.h"
#include "Widgets/Images/SImage.h"

void UHackMiniGameWidget::InitializeHackMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent,
	UPartnerHackComponent* InHackComponent, float InClickSuccessRangeMultiplier, float InSpinningGapMultiplier)
{
	if (ActiveMiniGameWidget && ActiveMiniGameWidget->IsMiniGameActive()) return;
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
	HackComponent = InHackComponent;
	ClickSuccessRangeMultiplier = FMath::IsFinite(InClickSuccessRangeMultiplier)
		? FMath::Max(InClickSuccessRangeMultiplier, 1.0f) : 1.0f;
	SpinningGapMultiplier = FMath::IsFinite(InSpinningGapMultiplier)
		? FMath::Max(InSpinningGapMultiplier, 1.0f) : 1.0f;
}

void UHackMiniGameWidget::SetMiniGameTimeLimit(float InTimeLimit)
{
	MiniGameTimeLimit = FMath::Max(0.0f, InTimeLimit);
}

bool UHackMiniGameWidget::StartHacking()
{
	ClearActiveMiniGame();

	TSubclassOf<UHackingMiniGameBase> MiniGameClass = ResolveMiniGameWidgetClass(); //Tag 읽고 미니게임 처리.
	bIsTimeLimited = ResolveIsTimeLimited();
	ElapsedTime = 0.0f;
	if (!MiniGameClass || !MiniGameRoot)
	{
		return false;
	}

	if (bIsTimeLimited && MiniGameTimeLimit <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[HackMiniGame] Limited minigame requires a positive time limit. Target=%s TimeLimit=%.2f"),
			*GetNameSafe(TargetActor),
			MiniGameTimeLimit);
		return false;
	}

	ActiveMiniGameWidget = CreateWidget<UHackingMiniGameBase>(this, MiniGameClass);
	if (!ActiveMiniGameWidget)
	{
		return false;
	}

	UCanvasPanelSlot* CanvasSlot = MiniGameRoot->AddChildToCanvas(ActiveMiniGameWidget);
	if (CanvasSlot)
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(0.0f));
	}

	// 실제 생성된 미니게임 타입(BP 파생 클래스 포함)에 맞는 배율 하나만 주입한다.
	const float SuccessRangeMultiplier = MiniGameClass->IsChildOf(USpinningCircleMiniGameWidget::StaticClass())
		? SpinningGapMultiplier : ClickSuccessRangeMultiplier;
	ActiveMiniGameWidget->InitializeMiniGame(TargetActor, HackableComponent, SuccessRangeMultiplier);
	ActiveMiniGameWidget->OnMiniGameFinished.AddDynamic(this, &UHackMiniGameWidget::HandleActiveMiniGameFinished);
	ApplyMiniGameCursor();
	ActiveMiniGameWidget->StartMiniGame();
	RefreshTimeProgressBar();

	return true;
}

void UHackMiniGameWidget::CancelHacking()
{
	if (ActiveMiniGameWidget && ActiveMiniGameWidget->IsMiniGameActive())
	{
		ActiveMiniGameWidget->FinishMiniGame(EHackResult::Cancelled);
		return;
	}

	FHackResultContext ResultContext;
	ResultContext.TargetActor = TargetActor;
	ResultContext.Result = EHackResult::Cancelled;
	RestoreCursor();
	OnHackMiniGameFinished.Broadcast(ResultContext);
}

void UHackMiniGameWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!MiniGameRoot && WidgetTree)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MiniGameRoot"));
		WidgetTree->RootWidget = RootCanvas;
		MiniGameRoot = RootCanvas;
	}

	if (MiniGameRoot)
	{
		MiniGameRoot->SetVisibility(ESlateVisibility::Visible);
	}
	if (FakeCursorImage)
	{
		FakeCursorImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (TimeProgressBar)
	{
		TimeProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		UpdateTimeProgress(1.0f);
	}
}

void UHackMiniGameWidget::NativeDestruct()
{
	ClearActiveMiniGame();
	Super::NativeDestruct();
}

void UHackMiniGameWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsTimeLimited
		|| !ActiveMiniGameWidget
		|| !ActiveMiniGameWidget->IsMiniGameActive())
	{
		return;
	}

	ElapsedTime += InDeltaTime;
	RefreshTimeProgressBar();

	if (ElapsedTime >= MiniGameTimeLimit)
	{
		ActiveMiniGameWidget->FinishMiniGame(EHackResult::Fail);
		return;
	}
}

FReply UHackMiniGameWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		&& ActiveMiniGameWidget
		&& ActiveMiniGameWidget->IsMiniGameActive()
		&& ActiveMiniGameWidget->HandlePrimaryClick())
	{
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

TSubclassOf<UHackingMiniGameBase> UHackMiniGameWidget::ResolveMiniGameWidgetClass() const
{
	if (HackableComponent)
	{
		for (const TPair<FGameplayTag, TSubclassOf<UHackingMiniGameBase>>& Pair : MiniGameWidgetClassesByTag)
		{
			if (Pair.Key.IsValid() && Pair.Value && HackableComponent->HackTags.HasTag(Pair.Key))
			{
				return Pair.Value;
			}
		}

		if (HackableComponent->HackTags.HasTag(HackGameplayTags::MiniGame::SpinningCircle()))
		{
			return USpinningCircleMiniGameWidget::StaticClass();
		}

		if (HackableComponent->HackTags.HasTag(HackGameplayTags::MiniGame::ClickCircle()))
		{
			return UClickCircleMiniGameWidget::StaticClass();
		}
	}

	if (DefaultMiniGameWidgetClass)
	{
		return DefaultMiniGameWidgetClass;
	}

	return UClickCircleMiniGameWidget::StaticClass();
}

bool UHackMiniGameWidget::ResolveIsTimeLimited() const
{
	if (!HackableComponent)
	{
		return true;
	}

	const bool bHasLimitedTag = HackableComponent->HackTags.HasTag(HackGameplayTags::Time::Limited());
	const bool bHasUnlimitedTag = HackableComponent->HackTags.HasTag(HackGameplayTags::Time::Unlimited());

	if (bHasLimitedTag && bHasUnlimitedTag)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[HackMiniGame] Target has both Limited and Unlimited time tags. Limited takes precedence. Target=%s"),
			*GetNameSafe(TargetActor));
		return true;
	}

	if (bHasUnlimitedTag)
	{
		return false;
	}

	// Preserve the existing timed behavior while targets are migrated to explicit time tags.
	return true;
}

void UHackMiniGameWidget::RefreshTimeProgressBar()
{
	if (!TimeProgressBar)
	{
		return;
	}

	TimeProgressBar->SetVisibility(
		bIsTimeLimited
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);

	if (!bIsTimeLimited)
	{
		return;
	}

	const float Progress = MiniGameTimeLimit > KINDA_SMALL_NUMBER
		? FMath::Clamp(ElapsedTime / MiniGameTimeLimit, 0.0f, 1.0f)
		: 1.0f;
	UpdateTimeProgress(Progress);
}

void UHackMiniGameWidget::EnsureProgressMaterial()
{
	if (ProgressMaterial || !TimeProgressBar)
	{
		return;
	}

	FProgressBarStyle ProgressStyle = TimeProgressBar->GetWidgetStyle();
	UMaterialInterface* Material = Cast<UMaterialInterface>(ProgressStyle.FillImage.GetResourceObject());
	if (!Material)
	{
		return;
	}

	ProgressMaterial = UMaterialInstanceDynamic::Create(Material, this);
	if (ProgressMaterial)
	{
		ProgressStyle.FillImage.SetResourceObject(ProgressMaterial);
		TimeProgressBar->SetWidgetStyle(ProgressStyle);
	}
}

void UHackMiniGameWidget::UpdateTimeProgress(float Progress)
{
	if (TimeProgressBar)
	{
		TimeProgressBar->SetPercent(1.0f - Progress);
	}

	EnsureProgressMaterial();
	if (ProgressMaterial)
	{
		ProgressMaterial->SetScalarParameterValue(ProgressParameterName, Progress);
	}
}

void UHackMiniGameWidget::ClearActiveMiniGame()
{
	RestoreCursor();

	if (ActiveMiniGameWidget)
	{
		ActiveMiniGameWidget->OnMiniGameFinished.RemoveDynamic(this, &UHackMiniGameWidget::HandleActiveMiniGameFinished);
		ActiveMiniGameWidget->RemoveFromParent();
		ActiveMiniGameWidget = nullptr;
	}

	ElapsedTime = 0.0f;

	if (TimeProgressBar)
	{
		TimeProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		UpdateTimeProgress(1.0f);
	}
}

void UHackMiniGameWidget::ApplyMiniGameCursor()
{
	RestoreCursor();
	if (!FakeCursorImage || !FakeCursorImage->GetBrush().GetResourceObject())
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	if (!Viewport)
	{
		return;
	}

	MiniGameCursorBrush = FakeCursorImage->GetBrush();
	if (const UCanvasPanelSlot* CursorSlot = Cast<UCanvasPanelSlot>(FakeCursorImage->Slot))
	{
		const FVector2D CursorSize = CursorSlot->GetSize();
		if (CursorSize.X > 0.0f && CursorSize.Y > 0.0f && !CursorSlot->GetAutoSize())
		{
			MiniGameCursorBrush.SetImageSize(CursorSize);
		}
	}

	PreviousCursorWidget = Viewport->GetSoftwareCursorWidget(EMouseCursor::Default);
	MiniGameCursorWidget = SNew(SImage)
		.Image(&MiniGameCursorBrush)
		.ColorAndOpacity(FakeCursorImage->GetColorAndOpacity())
		.Visibility(EVisibility::HitTestInvisible);
	CursorViewport = Viewport;
	Viewport->SetSoftwareCursorWidget(EMouseCursor::Default, MiniGameCursorWidget);
}

void UHackMiniGameWidget::RestoreCursor()
{
	if (UGameViewportClient* Viewport = CursorViewport.Get())
	{
		// Do not overwrite a cursor installed by another UI after this minigame.
		if (Viewport->GetSoftwareCursorWidget(EMouseCursor::Default) == MiniGameCursorWidget)
		{
			Viewport->SetSoftwareCursorWidget(EMouseCursor::Default, PreviousCursorWidget);
		}
	}
	CursorViewport.Reset();
	MiniGameCursorWidget.Reset();
	PreviousCursorWidget.Reset();
}

void UHackMiniGameWidget::HandleActiveMiniGameFinished(EHackResult Result)
{
	FHackResultContext ResultContext;
	ResultContext.TargetActor = TargetActor;
	ResultContext.Result = Result;
	RestoreCursor();
	OnHackMiniGameFinished.Broadcast(ResultContext);

	ClearActiveMiniGame();
}
