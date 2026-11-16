#include "UI/UpgradeMachineWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "Input/ControllerInputConfig.h"
#include "GameFramework/PlayerController.h"
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
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			// 대기 문구가 한 개여서 타이머가 없어도 키 재설정 직후 안내를 갱신한다.
			Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &ThisClass::UpdateWaitingMessage);
		}
	}
	bUIOpenedReported = false;
	bSessionSynchronized = false;
	bExitRequestInFlight = false;
	bExitFinalized = false;
	bFinalizedCloseRetryUsed = false;
	bLocalExitRequested = false;
	OpenedSessionId = 0;
	OpenedPlayerState.Reset();
	BindSessionState();
	if (Button)
	{
		Button->SetIsEnabled(false);
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
		SynchronizeSession();
	}
}

void UUpgradeMachineWidget::NativeDestruct()
{
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &ThisClass::UpdateWaitingMessage);
		}
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(WaitingMessageTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(FinalizedCloseRetryHandle);
	}
	UnbindSessionState();
	bSessionSynchronized = false;
	Super::NativeDestruct();
}

void UUpgradeMachineWidget::InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors)
{
	ShooterCharacter = ContextActors.IsValidIndex(0) ? Cast<AShooterCharacter>(ContextActors[0]) : nullptr;
	PartnerCharacter = ContextActors.IsValidIndex(1) ? Cast<APartnerCharacter>(ContextActors[1]) : nullptr;
	BindSessionState();
	if (bUIOpenedReported)
	{
		// 중복 Push는 NativeConstruct 없이 컨텍스트만 다시 주입하므로 여기서도 동기화한다.
		SynchronizeSession();
	}
	RefreshWaitingState();
}

bool UUpgradeMachineWidget::HandleUILayerEscape_Implementation()
{
	if (bLocalExitRequested)
	{
		CancelExit();
	}
	else
	{
		RequestExit();
	}
	return true;
}

bool UUpgradeMachineWidget::HandleUILayerConfirmed_Implementation()
{
	if (bSessionSynchronized && !bLocalExitRequested && !bExitRequestInFlight && !bExitFinalized)
	{
		HandleButtonClicked();
		return true;
	}
	return false;
}

void UUpgradeMachineWidget::HandleButtonClicked()
{
	if (bSessionSynchronized && !bLocalExitRequested && !bExitRequestInFlight && !bExitFinalized)
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

void UUpgradeMachineWidget::BindSessionState()
{
	UnbindSessionState();
	if (AOutlierPlayerState* PlayerState = GetLocalPlayerState())
	{
		PlayerState->OnStatAllocatorSessionChanged.AddUObject(this, &ThisClass::HandleSessionChanged);
		BoundSessionPlayerState = PlayerState;
	}
}

void UUpgradeMachineWidget::UnbindSessionState()
{
	if (AOutlierPlayerState* PlayerState = BoundSessionPlayerState.Get())
	{
		PlayerState->OnStatAllocatorSessionChanged.RemoveAll(this);
	}
	BoundSessionPlayerState.Reset();
}

void UUpgradeMachineWidget::HandleSessionChanged(
	uint32 SessionId, uint32 Generation, bool bPending, bool bFinalized, uint32 SyncRequestId)
{
	if (!bUIOpenedReported || Generation != OpenedGameplayGeneration || SessionId == 0)
	{
		return;
	}
	if (!bSessionSynchronized)
	{
		// 재사용 위젯에는 이전 화면의 응답도 도착할 수 있다. 이번 열기의 확인 응답만 받는다.
		if (SyncRequestId != SessionSyncRequestId)
		{
			return;
		}
		OpenedSessionId = SessionId;
		bSessionSynchronized = true;
	}
	if (SessionId != OpenedSessionId)
	{
		return;
	}
	bExitRequestInFlight = false;
	bLocalExitRequested = bPending;
	bExitFinalized = bFinalized;
	RefreshWaitingState();
	if (bExitFinalized)
	{
		TryPopMachineLayer();
	}
}

void UUpgradeMachineWidget::RefreshWaitingState()
{
	if (Button)
	{
		Button->SetIsEnabled(bSessionSynchronized && !bLocalExitRequested && !bExitRequestInFlight && !bExitFinalized);
	}
	if (TextBlock_76)
	{
		TextBlock_76->SetRenderOpacity(bLocalExitRequested ? 1.0f : 0.0f);
		if (bLocalExitRequested && WaitingMessages.IsValidIndex(WaitingMessageIndex))
		{
			UpdateWaitingMessage();
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

	OnWaitingStateChanged.Broadcast();
}

void UUpgradeMachineWidget::AdvanceWaitingMessage()
{
	if (!TextBlock_76 || WaitingMessages.IsEmpty())
	{
		return;
	}
	WaitingMessageIndex = (WaitingMessageIndex + 1) % WaitingMessages.Num();
	UpdateWaitingMessage();
}

void UUpgradeMachineWidget::SynchronizeSession()
{
	if (AOutlierPlayerState* PlayerState = OpenedPlayerState.Get())
	{
		bSessionSynchronized = false;
		bExitRequestInFlight = false;
		static uint32 NextSyncRequestId = 0;
		SessionSyncRequestId = ++NextSyncRequestId;
		if (SessionSyncRequestId == 0)
		{
			SessionSyncRequestId = ++NextSyncRequestId;
		}
		RefreshWaitingState();
		// 열기 RPC와 복제 순서 대신 이번 화면의 확인 응답으로 세션을 연결한다.
		PlayerState->SynchronizeStatAllocatorSession(SessionSyncRequestId);
	}
}

void UUpgradeMachineWidget::UpdateWaitingMessage()
{
	if (TextBlock_76 && WaitingMessages.IsValidIndex(WaitingMessageIndex))
	{
		const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		const ULocalPlayerUILayerSubsystem* Layers = LocalPlayer
			? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>() : nullptr;
		const UControllerInputConfig* Config = Layers ? Layers->GetWidgetInputConfig() : nullptr;
		const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer
			? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		TArray<FText> KeyNames;
		// 취소 입력을 라우팅하는 UI 레이어와 같은 액션·현재 매핑을 조회한다. 고정 Esc로 대체하지 않는다.
		if (Input && Config && Config->WidgetEscapeAction)
		{
			for (const FKey& Key : Input->QueryKeysMappedToAction(Config->WidgetEscapeAction))
			{
				if (Key.IsValid())
				{
					KeyNames.Add(Key.GetDisplayName(false));
				}
			}
		}
		const FText CancelKeys = KeyNames.IsEmpty()
			? NSLOCTEXT("UpgradeMachineWidget", "CancelKeyUnbound", "미지정")
			: FText::Join(FText::FromString(TEXT(" / ")), KeyNames);
		TextBlock_76->SetText(FText::Format(
			NSLOCTEXT("UpgradeMachineWidget", "WaitingCancelMapped", "{0}\n{1}: 취소"),
			WaitingMessages[WaitingMessageIndex], CancelKeys));
	}
}

void UUpgradeMachineWidget::RequestExit()
{
	if (!bSessionSynchronized || bLocalExitRequested || bExitRequestInFlight || bExitFinalized)
	{
		return;
	}
	if (AOutlierPlayerState* PlayerState = OpenedPlayerState.Get())
	{
		bExitRequestInFlight = true;
		RefreshWaitingState();
		PlayerState->RequestStatAllocatorExitPending(OpenedSessionId, OpenedGameplayGeneration, true);
	}
}

void UUpgradeMachineWidget::CancelExit()
{
	if (!CanCancelExit())
	{
		return;
	}
	if (AOutlierPlayerState* PlayerState = OpenedPlayerState.Get())
	{
		bExitRequestInFlight = true;
		RefreshWaitingState();
		// Layer와 강화 컨텍스트는 그대로 두고 서버의 본인 준비 상태만 해제한다.
		PlayerState->RequestStatAllocatorExitPending(OpenedSessionId, OpenedGameplayGeneration, false);
	}
}

void UUpgradeMachineWidget::TryPopMachineLayer()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem || !bExitFinalized)
	{
		return;
	}
	if (!LayerSubsystem->PopWidget(this))
	{
		// Listen/Standalone의 즉시 응답은 NativeConstruct 중 도착할 수 있다.
		// Layer 등록 직후 한 번만 다시 닫고, 제거 시 예약도 함께 해제한다.
		if (!bFinalizedCloseRetryUsed && GetWorld())
		{
			bFinalizedCloseRetryUsed = true;
			FinalizedCloseRetryHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(
				this, &ThisClass::TryPopMachineLayer);
		}
		return;
	}
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.Object.SavePoint.EXIT"), TEXT("Audio.Type.Interactable"));
	if (bUIOpenedReported)
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
