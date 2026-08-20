#include "UI/StatAllocatorWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UpgradeNodeGroupWidget.h"

void UStatAllocatorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (StartButton)
	{
		StartButton->OnClicked.AddUniqueDynamic(this, &UStatAllocatorWidget::HandleStartButtonClicked);
	}

	if (ESCButton)
	{
		ESCButton->OnClicked.AddUniqueDynamic(this, &UStatAllocatorWidget::HandleESCButtonClicked);
	}

	ShowIntroPage();
}

void UStatAllocatorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindOwningPlayerState();
	BindStatAllocatorExitPendingStates();
	ResetLocalStatAllocatorExitPending();
	RefreshStatAllocatorExitPendingState();
}

void UStatAllocatorWidget::NativeDestruct()
{
	UnbindStatAllocatorExitPendingStates();
	UnbindPlayerState();
	Super::NativeDestruct();
}

void UStatAllocatorWidget::InjectCharacters(
	AShooterCharacter* InShooterCharacter,
	APartnerCharacter* InPartnerCharacter)
{
	ShooterCharacter = InShooterCharacter;
	PartnerCharacter = InPartnerCharacter;

	if (GetCurrentPageIndex() == AllocatorPageIndex)
	{
		RefreshUpgradeNodeGroup();
	}
}

void UStatAllocatorWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	InjectCharacters(
		ContextActors.IsValidIndex(0)
			? Cast<AShooterCharacter>(ContextActors[0])
			: nullptr,
		ContextActors.IsValidIndex(1)
			? Cast<APartnerCharacter>(ContextActors[1])
			: nullptr);
	BindOwningPlayerState();
	BindStatAllocatorExitPendingStates();
	ResetLocalStatAllocatorExitPending();
	ShowIntroPage();
}

void UStatAllocatorWidget::ShowIntroPage()
{
	SetActivePage(IntroPageIndex);
}

void UStatAllocatorWidget::ShowAllocatorPage()
{
	SetActivePage(AllocatorPageIndex);
}

int32 UStatAllocatorWidget::GetCurrentPageIndex() const
{
	return StatAllocatorSwitcher
		? StatAllocatorSwitcher->GetActiveWidgetIndex()
		: INDEX_NONE;
}

void UStatAllocatorWidget::RefreshUpgradeNodeGroup()
{
	RebuildUpgradeNodeGroup();
}

bool UStatAllocatorWidget::HandleUILayerEscape_Implementation()
{
	if (GetCurrentPageIndex() == AllocatorPageIndex)
	{
		ShowIntroPage();
		return true;
	}

	if (GetCurrentPageIndex() == IntroPageIndex)
	{
		RequestStatAllocatorExit();
		return true;
	}

	return true;
}

bool UStatAllocatorWidget::HandleUILayerConfirmed_Implementation()
{
	if (GetCurrentPageIndex() == IntroPageIndex) //살짝 하드코딩.고정 인덱스니
	{
		//UE_LOG(LogTemp, Error, TEXT("HandleUILayerConfirmed_Called"));
		HandleStartButtonClicked();
	}
	else
	{
		return false;
	}

	return true;
}

bool UStatAllocatorWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool UStatAllocatorWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool UStatAllocatorWidget::HandleUILayerLeft_Implementation()
{
	return false;
}

bool UStatAllocatorWidget::HandleUILayerRight_Implementation()
{
	return false;
}

void UStatAllocatorWidget::HandleStartButtonClicked()
{
	if (IsLocalStatAllocatorExitPending())
	{
		return;
	}

	SetActivePage(AllocatorPageIndex);
}

void UStatAllocatorWidget::HandleESCButtonClicked()
{
	if (GetCurrentPageIndex() == AllocatorPageIndex)
	{
		ShowIntroPage();
		return;
	}

	RequestStatAllocatorExit();
}

void UStatAllocatorWidget::BindOwningPlayerState()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	AOutlierPlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (BoundPlayerState.Get() == PlayerState && NodeCountChangedHandle.IsValid())
	{
		return;
	}

	UnbindPlayerState();
	BoundPlayerState = PlayerState;

	if (PlayerState)
	{
		NodeCountChangedHandle = PlayerState->OnNodeCountChanged.AddUObject(
			this,
			&UStatAllocatorWidget::HandleNodeCountChanged);
	}

	BindStatAllocatorExitPendingStates();
}

void UStatAllocatorWidget::UnbindPlayerState()
{
	if (AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
		PlayerState && NodeCountChangedHandle.IsValid())
	{
		PlayerState->OnNodeCountChanged.Remove(NodeCountChangedHandle);
	}

	NodeCountChangedHandle.Reset();
	BoundPlayerState.Reset();
}

void UStatAllocatorWidget::BindStatAllocatorExitPendingStates()
{
	UnbindStatAllocatorExitPendingStates();

	AOutlierPlayerState* LocalPlayerState = GetLocalOutlierPlayerState();
	AOutlierPlayerState* PairedPlayerState = FindPairedOutlierPlayerState();

	if (LocalPlayerState)
	{
		LocalPlayerState->OnStatAllocatorExitPendingChanged.AddUObject(
			this,
			&UStatAllocatorWidget::HandleStatAllocatorExitPendingChanged);
		BoundExitPendingPlayerStates.Add(LocalPlayerState);
	}

	if (PairedPlayerState && PairedPlayerState != LocalPlayerState)
	{
		PairedPlayerState->OnStatAllocatorExitPendingChanged.AddUObject(
			this,
			&UStatAllocatorWidget::HandleStatAllocatorExitPendingChanged);
		BoundExitPendingPlayerStates.Add(PairedPlayerState);
	}
}

void UStatAllocatorWidget::UnbindStatAllocatorExitPendingStates()
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

void UStatAllocatorWidget::RefreshNodeCountText()
{
	if (!NodeCountText)
	{
		return;
	}

	const AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
	NodeCountText->SetText(FText::AsNumber(
		PlayerState ? PlayerState->GetNodeCount() : 0));
}

void UStatAllocatorWidget::HandleNodeCountChanged(int32 NewNodeCount)
{
	NodeCountText->SetText(FText::AsNumber(NewNodeCount));
}

void UStatAllocatorWidget::HandleStatAllocatorExitPendingChanged(AOutlierPlayerState* ChangedPlayerState)
{
	(void)ChangedPlayerState;
	RefreshStatAllocatorExitPendingState();
}

void UStatAllocatorWidget::RebuildUpgradeNodeGroup()
{
	UPanelWidget* Host = GetUpgradeNodeGroupHost();
	if (!Host)
	{
		return;
	}

	if (ActiveNodeGroupWidget)
	{
		ActiveNodeGroupWidget->RemoveFromParent();
	}
	ActiveNodeGroupWidget = nullptr;

	TSubclassOf<UUpgradeNodeGroupWidget> NodeGroupClass = ResolveNodeGroupWidgetClass();
	if (!NodeGroupClass)
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	ActiveNodeGroupWidget = CreateWidget<UUpgradeNodeGroupWidget>(OwningPlayer, NodeGroupClass);
	if (!ActiveNodeGroupWidget)
	{
		return;
	}

	if (UCanvasPanel* CanvasHost = Cast<UCanvasPanel>(Host))
	{
		if (UCanvasPanelSlot* CanvasSlot = CanvasHost->AddChildToCanvas(ActiveNodeGroupWidget))
		{
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			CanvasSlot->SetOffsets(FMargin(0.0f));
			CanvasSlot->SetAlignment(FVector2D::ZeroVector);
			CanvasSlot->SetZOrder(0);
		}
	}
	else
	{
		Host->AddChild(ActiveNodeGroupWidget);
	}

	AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
	if (!PlayerState && OwningPlayer)
	{
		PlayerState = OwningPlayer->GetPlayerState<AOutlierPlayerState>();
	}

	UOutlierUpgradeComponent* UpgradeComponent = nullptr;
	if (PlayerState && PlayerState->IsShooterPlayer() && ShooterCharacter)
	{
		UpgradeComponent = ShooterCharacter->GetUpgradeComponent();
	}
	else if (PlayerState && PlayerState->IsPartnerPlayer() && PartnerCharacter)
	{
		UpgradeComponent = PartnerCharacter->GetUpgradeComponent();
	}
	else if (ShooterCharacter)
	{
		UpgradeComponent = ShooterCharacter->GetUpgradeComponent();
	}
	else if (PartnerCharacter)
	{
		UpgradeComponent = PartnerCharacter->GetUpgradeComponent();
	}

	ActiveNodeGroupWidget->InjectUpgradeContext(
		ShooterCharacter,
		PartnerCharacter,
		UpgradeComponent,
		PlayerState);
}

UPanelWidget* UStatAllocatorWidget::GetUpgradeNodeGroupHost() const
{
	if (!StatAllocatorSwitcher
		|| AllocatorPageIndex < 0
		|| AllocatorPageIndex >= StatAllocatorSwitcher->GetNumWidgets())
	{
		return nullptr;
	}

	return Cast<UPanelWidget>(StatAllocatorSwitcher->GetWidgetAtIndex(AllocatorPageIndex));
}

TSubclassOf<UUpgradeNodeGroupWidget> UStatAllocatorWidget::ResolveNodeGroupWidgetClass() const
{
	const AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
	if (!PlayerState)
	{
		if (const APlayerController* PlayerController = GetOwningPlayer())
		{
			PlayerState = PlayerController->GetPlayerState<AOutlierPlayerState>();
		}
	}

	if (PlayerState)
	{
		if (PlayerState->IsShooterPlayer() && ShooterNodeGroupWidgetClass)
		{
			return ShooterNodeGroupWidgetClass;
		}

		if (PlayerState->IsPartnerPlayer() && PartnerNodeGroupWidgetClass)
		{
			return PartnerNodeGroupWidgetClass;
		}
	}

	if (ShooterCharacter && ShooterNodeGroupWidgetClass)
	{
		return ShooterNodeGroupWidgetClass;
	}

	if (PartnerCharacter && PartnerNodeGroupWidgetClass)
	{
		return PartnerNodeGroupWidgetClass;
	}

	return DefaultNodeGroupWidgetClass;
}

bool UStatAllocatorWidget::SetActivePage(int32 PageIndex)
{
	if (!StatAllocatorSwitcher
		|| PageIndex < 0
		|| PageIndex >= StatAllocatorSwitcher->GetNumWidgets())
	{
		return false;
	}

	StatAllocatorSwitcher->SetActiveWidgetIndex(PageIndex);

	if (PageIndex == AllocatorPageIndex)
	{
		BindOwningPlayerState();
		BindStatAllocatorExitPendingStates();
		RefreshNodeCountText();
		RefreshUpgradeNodeGroup();
	}
	else if (PageIndex == IntroPageIndex)
	{
		RefreshStatAllocatorExitPendingState();
	}

	return true;
}

AOutlierPlayerState* UStatAllocatorWidget::GetLocalOutlierPlayerState() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return PlayerController
		? PlayerController->GetPlayerState<AOutlierPlayerState>()
		: nullptr;
}

AOutlierPlayerState* UStatAllocatorWidget::FindPairedOutlierPlayerState() const
{
	const AOutlierPlayerState* LocalPlayerState = GetLocalOutlierPlayerState();
	const AGameStateBase* GameState = GetWorld()
		? GetWorld()->GetGameState()
		: nullptr;
	if (!LocalPlayerState || !GameState || LocalPlayerState->GetPairId() == INDEX_NONE)
	{
		return nullptr;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* CandidatePlayerState = Cast<AOutlierPlayerState>(RawPlayerState);
		if (CandidatePlayerState
			&& CandidatePlayerState != LocalPlayerState
			&& CandidatePlayerState->GetPairId() == LocalPlayerState->GetPairId())
		{
			return CandidatePlayerState;
		}
	}

	return nullptr;
}

void UStatAllocatorWidget::ResetLocalStatAllocatorExitPending()
{
	bLocalStatAllocatorExitRequested = false;

	if (AOutlierPlayerState* PlayerState = GetLocalOutlierPlayerState())
	{
		PlayerState->SetStatAllocatorExitPending(false);
	}
}

void UStatAllocatorWidget::RequestStatAllocatorExit()
{
	bLocalStatAllocatorExitRequested = true;

	if (AOutlierPlayerState* PlayerState = GetLocalOutlierPlayerState())
	{
		PlayerState->SetStatAllocatorExitPending(true);
	}

	RefreshStatAllocatorExitPendingState();
}

void UStatAllocatorWidget::RefreshStatAllocatorExitPendingState()
{
	const bool bLocalPending = IsLocalStatAllocatorExitPending();
	const bool bPairConfirmed = IsPairStatAllocatorExitConfirmed();

	if (WaitingText)
	{
		WaitingText->SetVisibility(
			bLocalPending && !bPairConfirmed
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}

	if (StartButton)
	{
		StartButton->SetIsEnabled(!bLocalPending);
	}

	if (bPairConfirmed)
	{
		TryPopStatAllocatorLayer();
	}
}

bool UStatAllocatorWidget::IsLocalStatAllocatorExitPending() const
{
	return bLocalStatAllocatorExitRequested;
}

bool UStatAllocatorWidget::IsPairStatAllocatorExitConfirmed() const
{
	const AOutlierPlayerState* LocalPlayerState = GetLocalOutlierPlayerState();
	const AOutlierPlayerState* PairedPlayerState = FindPairedOutlierPlayerState();
	return LocalPlayerState
		&& PairedPlayerState
		&& bLocalStatAllocatorExitRequested
		&& PairedPlayerState->IsStatAllocatorExitPending();
}

void UStatAllocatorWidget::TryPopStatAllocatorLayer()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopWidget(this);
	}
}
