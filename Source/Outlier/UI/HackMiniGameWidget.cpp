#include "UI/HackMiniGameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
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
	if (!MiniGameClass || !MiniGameRoot)
	{
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
	ActiveMiniGameWidget->SetTimeLimit(MiniGameTimeLimit);
	ActiveMiniGameWidget->OnMiniGameFinished.AddDynamic(this, &UHackMiniGameWidget::HandleActiveMiniGameFinished);
	ActiveMiniGameWidget->StartMiniGame();

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
}

void UHackMiniGameWidget::NativeDestruct()
{
	ClearActiveMiniGame();
	Super::NativeDestruct();
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

void UHackMiniGameWidget::ClearActiveMiniGame()
{
	if (!ActiveMiniGameWidget)
	{
		return;
	}

	ActiveMiniGameWidget->OnMiniGameFinished.RemoveDynamic(this, &UHackMiniGameWidget::HandleActiveMiniGameFinished);
	ActiveMiniGameWidget->RemoveFromParent();
	ActiveMiniGameWidget = nullptr;
}

void UHackMiniGameWidget::HandleActiveMiniGameFinished(EHackResult Result)
{
	FHackResultContext ResultContext;
	ResultContext.TargetActor = TargetActor;
	ResultContext.Result = Result;
	OnHackMiniGameFinished.Broadcast(ResultContext);

	ClearActiveMiniGame();
}
