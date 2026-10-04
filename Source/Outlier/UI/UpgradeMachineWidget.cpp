#include "UI/UpgradeMachineWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UpgradeNodeGroupWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"

UUpgradeMachineWidget::UUpgradeMachineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaitingMessages = {
		NSLOCTEXT("UpgradeMachineWidget", "WaitingOne", "다른 플레이어를 기다리는 중."),
		NSLOCTEXT("UpgradeMachineWidget", "WaitingTwo", "다른 플레이어를 기다리는 중.."),
		NSLOCTEXT("UpgradeMachineWidget", "WaitingThree", "다른 플레이어를 기다리는 중...")
	};
}

void UUpgradeMachineWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Button)
	{
		Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleButtonClicked);
	}
}

void UUpgradeMachineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bUIOpenedReported = false;
	OpenedPlayerState.Reset();
	BindExitPendingStates();
	if (Button)
	{
		Button->SetIsEnabled(true);
	}
	RefreshWaitingState();

	const UOutlierArenaSubsystem* Arena = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (AOutlierPlayerState* PlayerState = GetLocalPlayerState(); PlayerState && Arena)
	{
		OpenedPlayerState = PlayerState;
		OpenedGameplayGeneration = Arena->GetGameplayGeneration();
		bUIOpenedReported = true;
		PlayerState->ReportStatAllocatorUIOpened(OpenedGameplayGeneration);
	}
}

void UUpgradeMachineWidget::NativeDestruct()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(WaitingMessageTimerHandle);
	}
	UnbindExitPendingStates();
	Super::NativeDestruct();
}

void UUpgradeMachineWidget::InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors)
{
	ShooterCharacter = ContextActors.IsValidIndex(0) ? Cast<AShooterCharacter>(ContextActors[0]) : nullptr;
	PartnerCharacter = ContextActors.IsValidIndex(1) ? Cast<APartnerCharacter>(ContextActors[1]) : nullptr;
	bLocalExitRequested = false;
	if (AOutlierPlayerState* PlayerState = GetLocalPlayerState())
	{
		PlayerState->SetStatAllocatorExitPending(false);
	}
	BindExitPendingStates();
	if (Button)
	{
		Button->SetIsEnabled(true);
	}
	RefreshWaitingState();
}

bool UUpgradeMachineWidget::HandleUILayerEscape_Implementation()
{
	RequestExit();
	return true;
}

bool UUpgradeMachineWidget::HandleUILayerConfirmed_Implementation()
{
	if (!bLocalExitRequested)
	{
		HandleButtonClicked();
		return true;
	}
	return false;
}

void UUpgradeMachineWidget::HandleButtonClicked()
{
	if (!bLocalExitRequested)
	{
		OpenNodeScreen();
	}
}

void UUpgradeMachineWidget::OpenNodeScreen()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !GetOwningPlayer())
	{
		return;
	}
	if (ActiveNodeGroupWidget && LayerSubsystem->GetTopLayerWidget() == ActiveNodeGroupWidget)
	{
		return;
	}

	const TSubclassOf<UUpgradeNodeGroupWidget> GroupClass = ResolveNodeGroupClass();
	if (!GroupClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UpgradeMachine] Node group class is not configured for %s"), *GetName());
		return;
	}

	if (!ActiveNodeGroupWidget || ActiveNodeGroupWidget->GetClass() != GroupClass.Get())
	{
		ActiveNodeGroupWidget = CreateWidget<UUpgradeNodeGroupWidget>(GetOwningPlayer(), GroupClass);
		if (!ActiveNodeGroupWidget)
		{
			return;
		}
	}

	AOutlierPlayerState* PlayerState = GetLocalPlayerState();
	UOutlierUpgradeComponent* UpgradeComponent = nullptr;
	if (PlayerState && PlayerState->IsShooterPlayer() && ShooterCharacter)
	{
		UpgradeComponent = ShooterCharacter->GetUpgradeComponent();
	}
	else if (PlayerState && PlayerState->IsPartnerPlayer() && PartnerCharacter)
	{
		UpgradeComponent = PartnerCharacter->GetUpgradeComponent();
	}
	ActiveNodeGroupWidget->InjectUpgradeContext(
		ShooterCharacter, PartnerCharacter, UpgradeComponent, PlayerState);
	LayerSubsystem->PushWidget(
		UILayerTags::GameMenu(), ActiveNodeGroupWidget,
		FirstPersonInputModeTags::UI(), this,
		EUILayerFocusTarget::Widget, true, true);
}

void UUpgradeMachineWidget::BindExitPendingStates()
{
	UnbindExitPendingStates();
	AOutlierPlayerState* LocalPlayerState = GetLocalPlayerState();
	AOutlierPlayerState* PairedPlayerState = FindPairedPlayerState();
	if (LocalPlayerState)
	{
		LocalPlayerState->OnStatAllocatorExitPendingChanged.AddUObject(this, &ThisClass::HandleExitPendingChanged);
		BoundExitPendingPlayerStates.Add(LocalPlayerState);
	}
	if (PairedPlayerState && PairedPlayerState != LocalPlayerState)
	{
		PairedPlayerState->OnStatAllocatorExitPendingChanged.AddUObject(this, &ThisClass::HandleExitPendingChanged);
		BoundExitPendingPlayerStates.Add(PairedPlayerState);
	}
}

void UUpgradeMachineWidget::UnbindExitPendingStates()
{
	for (const TWeakObjectPtr<AOutlierPlayerState>& PlayerStatePtr : BoundExitPendingPlayerStates)
	{
		if (AOutlierPlayerState* PlayerState = PlayerStatePtr.Get())
		{
			PlayerState->OnStatAllocatorExitPendingChanged.RemoveAll(this);
		}
	}
	BoundExitPendingPlayerStates.Reset();
}

void UUpgradeMachineWidget::HandleExitPendingChanged(AOutlierPlayerState* ChangedPlayerState)
{
	(void)ChangedPlayerState;
	RefreshWaitingState();
}

void UUpgradeMachineWidget::RefreshWaitingState()
{
	const AOutlierPlayerState* PairedPlayerState = FindPairedPlayerState();
	const bool bOtherExiting = PairedPlayerState && PairedPlayerState->IsStatAllocatorExitPending();
	if (TextBlock_76)
	{
		TextBlock_76->SetRenderOpacity(bLocalExitRequested ? 1.0f : 0.0f);
		if (bLocalExitRequested && WaitingMessages.IsValidIndex(WaitingMessageIndex))
		{
			TextBlock_76->SetText(WaitingMessages[WaitingMessageIndex]);
		}
	}

	if (GetWorld())
	{
		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		if (bLocalExitRequested && WaitingMessages.Num() > 1)
		{
			if (!TimerManager.IsTimerActive(WaitingMessageTimerHandle))
			{
				TimerManager.SetTimer(WaitingMessageTimerHandle, this, &ThisClass::AdvanceWaitingMessage,
					FMath::Max(WaitingMessageInterval, 0.1f), true);
			}
		}
		else
		{
			TimerManager.ClearTimer(WaitingMessageTimerHandle);
			WaitingMessageIndex = 0;
		}
	}

	if (bLocalExitRequested && bOtherExiting)
	{
		TryPopMachineLayer();
	}
}

void UUpgradeMachineWidget::AdvanceWaitingMessage()
{
	if (!TextBlock_76 || WaitingMessages.IsEmpty())
	{
		return;
	}
	WaitingMessageIndex = (WaitingMessageIndex + 1) % WaitingMessages.Num();
	TextBlock_76->SetText(WaitingMessages[WaitingMessageIndex]);
}

void UUpgradeMachineWidget::RequestExit()
{
	if (bLocalExitRequested)
	{
		return;
	}
	bLocalExitRequested = true;
	if (Button)
	{
		Button->SetIsEnabled(false);
	}
	if (AOutlierPlayerState* PlayerState = GetLocalPlayerState())
	{
		PlayerState->SetStatAllocatorExitPending(true);
	}
	RefreshWaitingState();
}

void UUpgradeMachineWidget::TryPopMachineLayer()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem && LayerSubsystem->PopWidget(this) && bUIOpenedReported)
	{
		bUIOpenedReported = false;
		if (AOutlierPlayerState* PlayerState = OpenedPlayerState.Get())
		{
			PlayerState->ReportStatAllocatorUIClosed(OpenedGameplayGeneration);
		}
	}
}

AOutlierPlayerState* UUpgradeMachineWidget::GetLocalPlayerState() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return PlayerController ? PlayerController->GetPlayerState<AOutlierPlayerState>() : nullptr;
}

AOutlierPlayerState* UUpgradeMachineWidget::FindPairedPlayerState() const
{
	const AOutlierPlayerState* LocalPlayerState = GetLocalPlayerState();
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!LocalPlayerState || !GameState || LocalPlayerState->GetPairId() == INDEX_NONE)
	{
		return nullptr;
	}
	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* Candidate = Cast<AOutlierPlayerState>(RawPlayerState);
		if (Candidate && Candidate != LocalPlayerState
			&& Candidate->GetPairId() == LocalPlayerState->GetPairId())
		{
			return Candidate;
		}
	}
	return nullptr;
}

TSubclassOf<UUpgradeNodeGroupWidget> UUpgradeMachineWidget::ResolveNodeGroupClass() const
{
	const AOutlierPlayerState* PlayerState = GetLocalPlayerState();
	if (PlayerState && PlayerState->IsShooterPlayer() && ShooterNodeGroupWidgetClass)
	{
		return ShooterNodeGroupWidgetClass;
	}
	if (PlayerState && PlayerState->IsPartnerPlayer() && PartnerNodeGroupWidgetClass)
	{
		return PartnerNodeGroupWidgetClass;
	}
	return DefaultNodeGroupWidgetClass;
}
