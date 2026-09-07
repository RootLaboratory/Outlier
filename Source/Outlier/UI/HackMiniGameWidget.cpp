#include "UI/HackMiniGameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Input/Reply.h"
#include "UI/ClickCircleMiniGameWidget.h"
#include "UI/HackingMiniGameBase.h"
#include "UI/SpinningCircleMiniGameWidget.h"

void UHackMiniGameWidget::InitializeHackMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;
	HackComponent = InHackComponent;
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

	ActiveMiniGameWidget = CreateWidget<UHackingMiniGameBase>(GetOwningPlayer(), MiniGameClass);
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

	ActiveMiniGameWidget->InitializeMiniGame(TargetActor, HackableComponent);
	ActiveMiniGameWidget->OnMiniGameFinished.AddDynamic(this, &UHackMiniGameWidget::HandleActiveMiniGameFinished);
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

	if (TimeProgressBar)
	{
		TimeProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		TimeProgressBar->SetPercent(0.0f);
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

	const float RemainingRatio = MiniGameTimeLimit > KINDA_SMALL_NUMBER
		? 1.0f - FMath::Clamp(ElapsedTime / MiniGameTimeLimit, 0.0f, 1.0f)
		: 0.0f;
	TimeProgressBar->SetPercent(RemainingRatio);

	UE_LOG(LogTemp, Error, TEXT("Called and left, %f"), RemainingRatio);

}

void UHackMiniGameWidget::ClearActiveMiniGame()
{
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
		TimeProgressBar->SetPercent(0.0f);
	}
}

void UHackMiniGameWidget::HandleActiveMiniGameFinished(EHackResult Result)
{
	FHackResultContext ResultContext;
	ResultContext.TargetActor = TargetActor;
	ResultContext.Result = Result;
	OnHackMiniGameFinished.Broadcast(ResultContext);

	ClearActiveMiniGame();
}
