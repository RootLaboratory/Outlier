#include "UI/LocalPlayerUILayerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "FrontendPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "MainUIBase.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "UI/UILayerRootWidget.h"

void ULocalPlayerUILayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULocalPlayerUILayerSubsystem::Deinitialize()
{
	bIsDeinitializing = true;

	for (FUILayerEntry& Entry : LayerEntries)
	{
		if (UWidget* HitTestBlocker = Entry.HitTestBlocker.Get())
		{
			HitTestBlocker->RemoveFromParent();
		}

		if (UUserWidget* Widget = Entry.Widget.Get())
		{
			Widget->RemoveFromParent();
		}
	}

	LayerEntries.Reset();
	CachedTopLayerHandle.Reset();
	CachedInputWidget.Reset();
	CachedInputModeTag = FGameplayTag();
	DestroyLayerRoot();
	RegisteredMainUI.Reset();

	Super::Deinitialize();
}

void ULocalPlayerUILayerSubsystem::RegisterMainUI(UMainUIBase* MainUI)
{
	if (!IsValid(MainUI))
	{
		return;
	}

	RegisteredMainUI = MainUI;
	EnsureLayerRoot();
}

void ULocalPlayerUILayerSubsystem::UnregisterMainUI(UMainUIBase* MainUI)
{
	if (RegisteredMainUI.Get() != MainUI)
	{
		return;
	}

	ClearAllLayers();
	DestroyLayerRoot();
	RegisteredMainUI.Reset();
}

bool ULocalPlayerUILayerSubsystem::EnsureLayerRoot()
{
	if (IsValid(LayerRootWidget))
	{
		return true;
	}

	APlayerController* PlayerController = GetLocalPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	//Layer 관리를 위해서 만드는 Canvas.

	LayerRootWidget = CreateWidget<UUILayerRootWidget>(
		PlayerController,
		UUILayerRootWidget::StaticClass());

	if (!LayerRootWidget)
	{
		return false;
	}

	LayerRootWidget->AddToViewport(1000);

	RegisterLayerContainer(UILayerTags::Gameplay(), LayerRootWidget->GetGameplayLayer());
	RegisterLayerContainer(UILayerTags::GameMenu(), LayerRootWidget->GetGameMenuLayer());
	RegisterLayerContainer(UILayerTags::Modal(), LayerRootWidget->GetModalLayer());
	RegisterLayerContainer(UILayerTags::System(), LayerRootWidget->GetSystemLayer());
	return true;
}

void ULocalPlayerUILayerSubsystem::DestroyLayerRoot()
{
	LayerContainers.Reset();

	if (IsValid(LayerRootWidget))
	{
		LayerRootWidget->RemoveFromParent();
	}

	LayerRootWidget = nullptr;
}

void ULocalPlayerUILayerSubsystem::RegisterLayerContainer(
	FGameplayTag LayerTag,
	UPanelWidget* Container)
{
	if (!LayerTag.IsValid() || !IsValid(Container))
	{
		return;
	}

	LayerContainers.Add(LayerTag, Container);
}

void ULocalPlayerUILayerSubsystem::UnregisterLayerContainer(
	FGameplayTag LayerTag,
	UPanelWidget* Container)
{
	if (const TObjectPtr<UPanelWidget>* RegisteredContainer = LayerContainers.Find(LayerTag);
		RegisteredContainer && RegisteredContainer->Get() == Container)
	{
		LayerContainers.Remove(LayerTag);
	}
}

FUILayerHandle ULocalPlayerUILayerSubsystem::PushWidget(
	const FUILayerPushRequest& Request)
{
	if (!Request.WidgetClass
		|| !Request.LayerTag.IsValid()
		|| !Request.InputModeTag.IsValid())
	{
		return {};
	}

	for (const FUILayerEntry& ExistingEntry : LayerEntries)
	{
		UUserWidget* ExistingWidget = ExistingEntry.Widget.Get();
		if (ExistingEntry.RequestOwner.Get() == Request.RequestOwner
			&& IsValid(ExistingWidget)
			&& ExistingWidget->IsA(Request.WidgetClass))
		{
			if (ExistingWidget->GetClass()->ImplementsInterface(
				UUILayerContextReceiver::StaticClass()))
			{
				TArray<AActor*> ContextActors;
				ContextActors.Reserve(Request.ContextActors.Num());
				for (AActor* ContextActor : Request.ContextActors)
				{
					ContextActors.Add(ContextActor);
				}

				IUILayerContextReceiver::Execute_InitializeUILayerContext(
					ExistingWidget,
					ContextActors);
			}

			return ExistingEntry.Handle;
		}
	}

	APlayerController* PlayerController = GetLocalPlayerController();
	if (!PlayerController)
	{
		return {};
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(
		PlayerController,
		Request.WidgetClass);
	if (!Widget)
	{
		return {};
	}

	if (Widget->GetClass()->ImplementsInterface(UUILayerContextReceiver::StaticClass()))
	{
		TArray<AActor*> ContextActors;
		ContextActors.Reserve(Request.ContextActors.Num());
		for (AActor* ContextActor : Request.ContextActors)
		{
			ContextActors.Add(ContextActor);
		}

		IUILayerContextReceiver::Execute_InitializeUILayerContext(
			Widget,
			ContextActors);
	}

	return PushWidget(
		Request.LayerTag,
		Widget,
		Request.InputModeTag,
		Request.RequestOwner,
		Request.FocusTarget,
		Request.bShowCursor,
		Request.bReceivesInput);
}

FUILayerHandle ULocalPlayerUILayerSubsystem::PushWidget(
	FGameplayTag LayerTag,
	UUserWidget* Widget,
	FGameplayTag InputModeTag,
	UObject* RequestOwner,
	EUILayerFocusTarget FocusTarget,
	bool bShowCursor,
	bool bReceivesInput)
{
	if (!LayerTag.IsValid() || !InputModeTag.IsValid() || !IsValid(Widget))
	{
		return {};
	}

	for (const FUILayerEntry& ExistingEntry : LayerEntries)
	{
		if (ExistingEntry.Widget.Get() == Widget)
		{
			return ExistingEntry.Handle;
		}
	}

	if (!EnsureLayerRoot())
	{
		return {};
	}

	const FUILayerHandle Handle{NextLayerId++};
	UWidget* HitTestBlocker = nullptr;
	if (!AttachWidgetToLayer(
		LayerTag,
		Widget,
		FocusTarget,
		Handle.Id,
		HitTestBlocker))
	{
		return {};
	}

	FUILayerEntry& Entry = LayerEntries.AddDefaulted_GetRef();
	Entry.Handle = Handle;
	Entry.LayerTag = LayerTag;
	Entry.InputModeTag = InputModeTag;
	Entry.Widget = Widget;
	Entry.HitTestBlocker = HitTestBlocker;
	Entry.RequestOwner = RequestOwner;
	Entry.FocusTarget = FocusTarget;
	Entry.bShowCursor = bShowCursor;
	Entry.bReceivesInput = bReceivesInput;

	RefreshTopLayerInput();
	return Handle;
}

FUILayerHandle ULocalPlayerUILayerSubsystem::PushInputScope(
	FGameplayTag LayerTag,
	FGameplayTag InputModeTag,
	UObject* RequestOwner,
	EUILayerFocusTarget FocusTarget,
	bool bShowCursor,
	bool bReceivesInput)
{
	if (!LayerTag.IsValid() || !InputModeTag.IsValid())
	{
		return {};
	}

	FUILayerEntry& Entry = LayerEntries.AddDefaulted_GetRef();
	Entry.Handle = FUILayerHandle{NextLayerId++};
	Entry.LayerTag = LayerTag;
	Entry.InputModeTag = InputModeTag;
	Entry.RequestOwner = RequestOwner;
	Entry.FocusTarget = FocusTarget;
	Entry.bShowCursor = bShowCursor;
	Entry.bReceivesInput = bReceivesInput;

	RefreshTopLayerInput();
	return Entry.Handle;
}

FUILayerHandle ULocalPlayerUILayerSubsystem::ReplaceWidget(
	FUILayerHandle PreviousHandle,
	FGameplayTag LayerTag,
	UUserWidget* Widget,
	FGameplayTag InputModeTag,
	UObject* RequestOwner,
	EUILayerFocusTarget FocusTarget,
	bool bShowCursor,
	bool bReceivesInput)
{
	if (!PreviousHandle.IsValid()
		|| !LayerTag.IsValid()
		|| !InputModeTag.IsValid()
		|| !IsValid(Widget)
		|| !EnsureLayerRoot())
	{
		return {};
	}

	const int32 Index = LayerEntries.IndexOfByPredicate(
		[PreviousHandle](const FUILayerEntry& Entry)
		{
			return Entry.Handle == PreviousHandle;
		});
	if (Index == INDEX_NONE)
	{
		return {};
	}

	const FUILayerHandle NewHandle{NextLayerId++};
	UWidget* HitTestBlocker = nullptr;
	if (!AttachWidgetToLayer(
		LayerTag,
		Widget,
		FocusTarget,
		NewHandle.Id,
		HitTestBlocker))
	{
		return {};
	}

	if (UWidget* PreviousBlocker = LayerEntries[Index].HitTestBlocker.Get())
	{
		PreviousBlocker->RemoveFromParent();
	}

	if (UUserWidget* PreviousWidget = LayerEntries[Index].Widget.Get();
		PreviousWidget && PreviousWidget != Widget)
	{
		PreviousWidget->RemoveFromParent();
	}

	FUILayerEntry& Entry = LayerEntries[Index];
	Entry.Handle = NewHandle;
	Entry.LayerTag = LayerTag;
	Entry.InputModeTag = InputModeTag;
	Entry.Widget = Widget;
	Entry.HitTestBlocker = HitTestBlocker;
	Entry.RequestOwner = RequestOwner;
	Entry.FocusTarget = FocusTarget;
	Entry.bShowCursor = bShowCursor;
	Entry.bReceivesInput = bReceivesInput;

	RefreshTopLayerInput();
	return NewHandle;
}

bool ULocalPlayerUILayerSubsystem::PopLayer(FUILayerHandle Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	const int32 Index = LayerEntries.IndexOfByPredicate(
		[Handle](const FUILayerEntry& Entry)
		{
			return Entry.Handle == Handle;
		});

	if (Index == INDEX_NONE)
	{
		return false;
	}

	if (UUserWidget* Widget = LayerEntries[Index].Widget.Get())
	{
		Widget->RemoveFromParent();
	}

	if (UWidget* HitTestBlocker = LayerEntries[Index].HitTestBlocker.Get())
	{
		HitTestBlocker->RemoveFromParent();
	}

	LayerEntries.RemoveAt(Index);
	RefreshTopLayerInput();
	return true;
}

bool ULocalPlayerUILayerSubsystem::PopWidget(UUserWidget* Widget)
{
	if (!IsValid(Widget))
	{
		return false;
	}

	const int32 Index = LayerEntries.IndexOfByPredicate(
		[Widget](const FUILayerEntry& Entry)
		{
			return Entry.Widget.Get() == Widget;
		});

	if (Index == INDEX_NONE)
	{
		return false;
	}

	return PopLayer(LayerEntries[Index].Handle);
}

int32 ULocalPlayerUILayerSubsystem::PopLayersByOwner(UObject* RequestOwner)
{
	if (!RequestOwner)
	{
		return 0;
	}

	int32 RemovedCount = 0;
	for (int32 Index = LayerEntries.Num() - 1; Index >= 0; --Index)
	{
		if (LayerEntries[Index].RequestOwner.Get() != RequestOwner)
		{
			continue;
		}

		if (UUserWidget* Widget = LayerEntries[Index].Widget.Get())
		{
			Widget->RemoveFromParent();
		}

		if (UWidget* HitTestBlocker = LayerEntries[Index].HitTestBlocker.Get())
		{
			HitTestBlocker->RemoveFromParent();
		}

		LayerEntries.RemoveAt(Index);
		++RemovedCount;
	}

	if (RemovedCount > 0)
	{
		RefreshTopLayerInput();
	}

	return RemovedCount;
}

void ULocalPlayerUILayerSubsystem::ClearAllLayers()
{
	for (FUILayerEntry& Entry : LayerEntries)
	{
		if (UWidget* HitTestBlocker = Entry.HitTestBlocker.Get())
		{
			HitTestBlocker->RemoveFromParent();
		}

		if (UUserWidget* Widget = Entry.Widget.Get())
		{
			Widget->RemoveFromParent();
		}
	}

	LayerEntries.Reset();
	CachedTopLayerHandle.Reset();
	CachedInputWidget.Reset();
	CachedInputModeTag = FGameplayTag();
	if (!bIsDeinitializing)
	{
		ApplyDefaultInput();
	}
}

bool ULocalPlayerUILayerSubsystem::RefocusLayer(
	FUILayerHandle Handle,
	EUILayerFocusTarget FocusTarget)
{
	FUILayerEntry* Entry = FindLayer(Handle);
	if (!Entry)
	{
		return false;
	}

	Entry->FocusTarget = FocusTarget;
	if (CachedTopLayerHandle == Handle)
	{
		ApplyLayerInput(*Entry);
	}

	return true;
}

bool ULocalPlayerUILayerSubsystem::IsLayerActive(FGameplayTag LayerTag) const
{
	return LayerEntries.ContainsByPredicate(
		[LayerTag](const FUILayerEntry& Entry)
		{
			return Entry.LayerTag.MatchesTagExact(LayerTag);
		});
}

UUserWidget* ULocalPlayerUILayerSubsystem::GetTopLayerWidget() const
{
	return CachedInputWidget.Get();
}

FGameplayTag ULocalPlayerUILayerSubsystem::GetActiveInputModeTag() const
{
	return CachedInputModeTag;
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetEscapeInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerEscape,
		true);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetConfirmedInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerConfirmed,
		false);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetUpInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerUp,
		false);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetDownInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerDown,
		false);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetLeftInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerLeft,
		false);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetRightInput()
{
	return RouteWidgetInput(
		&IUILayerInputReceiver::Execute_HandleUILayerRight,
		false);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetInput(
	bool (*InputExecutor)(UObject*),
	bool bPopUnhandledInput)
{
	UUserWidget* InputWidget = CachedInputWidget.Get();
	if (!IsValid(InputWidget)
		|| !InputWidget->GetClass()->ImplementsInterface(UUILayerInputReceiver::StaticClass()))
	{
		return false;
	}

	const FUILayerHandle RoutedLayerHandle = CachedTopLayerHandle;
	if (InputExecutor(InputWidget))
	{
		return true;
	}

	if (!bPopUnhandledInput)
	{
		return false;
	}

	// An unhandled escape request closes only the layer that received the input.
	return PopLayer(RoutedLayerHandle);
}

bool ULocalPlayerUILayerSubsystem::AttachWidgetToLayer(
	FGameplayTag LayerTag,
	UUserWidget* Widget,
	EUILayerFocusTarget FocusTarget,
	int32 ZOrder,
	UWidget*& OutHitTestBlocker)
{
	OutHitTestBlocker = nullptr;

	const TObjectPtr<UPanelWidget>* ContainerPtr = LayerContainers.Find(LayerTag);
	UPanelWidget* Container = ContainerPtr ? ContainerPtr->Get() : nullptr;
	if (!Container || !Widget)
	{
		return false;
	}

	if (Widget->GetParent())
	{
		Widget->RemoveFromParent();
	}

	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Container))
	{
		const int32 BaseZOrder = ZOrder * 2;
		if (FocusTarget == EUILayerFocusTarget::Widget)
		{
			UButton* HitTestBlocker = NewObject<UButton>(Canvas);
			if (!HitTestBlocker)
			{
				return false;
			}

			FButtonStyle TransparentButtonStyle;
			FSlateBrush TransparentBrush;
			TransparentBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			TransparentBrush.TintColor = FSlateColor(FLinearColor::Transparent);
			TransparentButtonStyle
				.SetNormal(TransparentBrush)
				.SetHovered(TransparentBrush)
				.SetPressed(TransparentBrush)
				.SetDisabled(TransparentBrush);

			HitTestBlocker->SetStyle(TransparentButtonStyle);
			HitTestBlocker->SetVisibility(ESlateVisibility::Visible);

			if (UCanvasPanelSlot* BlockerSlot = Canvas->AddChildToCanvas(HitTestBlocker))
			{
				BlockerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BlockerSlot->SetOffsets(FMargin(0.0f));
				BlockerSlot->SetAlignment(FVector2D::ZeroVector);
				BlockerSlot->SetAutoSize(false);
				BlockerSlot->SetZOrder(BaseZOrder);
				OutHitTestBlocker = HitTestBlocker;
			}
			else
			{
				return false;
			}
		}

		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetAlignment(FVector2D::ZeroVector);
			Slot->SetAutoSize(false);
			Slot->SetZOrder(BaseZOrder + 1);
			return true;
		}

		if (OutHitTestBlocker)
		{
			OutHitTestBlocker->RemoveFromParent();
			OutHitTestBlocker = nullptr;
		}
		return false;
	}

	return Container->AddChild(Widget) != nullptr;
}

void ULocalPlayerUILayerSubsystem::RemoveInvalidLayers()
{
	LayerEntries.RemoveAll(
		[](const FUILayerEntry& Entry)
		{
			const bool bWidgetWasDestroyed = Entry.Widget.IsExplicitlyNull() == false
				&& !Entry.Widget.IsValid();
			const bool bOwnerWasDestroyed = Entry.RequestOwner.IsExplicitlyNull() == false
				&& !Entry.RequestOwner.IsValid();
			return bWidgetWasDestroyed || bOwnerWasDestroyed;
		});
}

const FUILayerEntry* ULocalPlayerUILayerSubsystem::FindTopInputLayer() const
{
	const FUILayerEntry* BestEntry = nullptr;
	int32 BestPriority = MIN_int32;

	for (const FUILayerEntry& Entry : LayerEntries)
	{
		if (!Entry.bReceivesInput)
		{
			continue;
		}

		const int32 Priority = GetLayerPriority(Entry.LayerTag);
		if (!BestEntry
			|| Priority > BestPriority
			|| (Priority == BestPriority && Entry.Handle.Id > BestEntry->Handle.Id))
		{
			BestEntry = &Entry;
			BestPriority = Priority;
		}
	}

	return BestEntry;
}

FUILayerEntry* ULocalPlayerUILayerSubsystem::FindLayer(FUILayerHandle Handle)
{
	return LayerEntries.FindByPredicate(
		[Handle](const FUILayerEntry& Entry)
		{
			return Entry.Handle == Handle;
		});
}

int32 ULocalPlayerUILayerSubsystem::GetLayerPriority(FGameplayTag LayerTag) const
{
	if (LayerTag.MatchesTagExact(UILayerTags::System()))
	{
		return 400;
	}
	if (LayerTag.MatchesTagExact(UILayerTags::Modal()))
	{
		return 300;
	}
	if (LayerTag.MatchesTagExact(UILayerTags::GameMenu()))
	{
		return 200;
	}
	return 100;
}

void ULocalPlayerUILayerSubsystem::RefreshTopLayerInput()
{
	RemoveInvalidLayers();

	if (const FUILayerEntry* TopLayer = FindTopInputLayer())
	{
		CachedTopLayerHandle = TopLayer->Handle;
		CachedInputWidget = TopLayer->Widget;
		CachedInputModeTag = TopLayer->InputModeTag;
		ApplyLayerInput(*TopLayer);
	}
	else
	{
		CachedTopLayerHandle.Reset();
		CachedInputWidget.Reset();
		CachedInputModeTag = FGameplayTag();
		ApplyDefaultInput();
	}
}

void ULocalPlayerUILayerSubsystem::ApplyLayerInput(const FUILayerEntry& Layer)
{
	APlayerController* PlayerController = GetLocalPlayerController();
	if (!PlayerController)
	{
		return;
	}

	bool bInputModeApplied = false;
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(PlayerController))
	{
		bInputModeApplied = FirstPersonController->SetFirstPersonInputMode(Layer.InputModeTag);
	}
	else if (AFrontendPlayerController* FrontendController =
		Cast<AFrontendPlayerController>(PlayerController))
	{
		bInputModeApplied = FrontendController->SetFrontendInputMode(Layer.InputModeTag);
	}

	if (!bInputModeApplied)
	{
		return;
	}

	FInputModeGameAndUI InputMode;
	if (Layer.FocusTarget == EUILayerFocusTarget::Widget)
	{
		if (UUserWidget* Widget = Layer.Widget.Get())
		{
			InputMode.SetWidgetToFocus(Widget->TakeWidget());
		}
	}

	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = Layer.bShowCursor;

	if (Layer.FocusTarget == EUILayerFocusTarget::GameViewport
		&& FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ULocalPlayerUILayerSubsystem::ApplyDefaultInput()
{
	APlayerController* PlayerController = GetLocalPlayerController();
	if (AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(PlayerController))
	{
		FirstPersonController->RestoreFirstPersonDefaultInputMode();
	}
	else if (AFrontendPlayerController* FrontendController =
		Cast<AFrontendPlayerController>(PlayerController))
	{
		FrontendController->RestoreFrontendDefaultInputMode();
	}
}

APlayerController* ULocalPlayerUILayerSubsystem::GetLocalPlayerController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer && GetWorld()
		? LocalPlayer->GetPlayerController(GetWorld())
		: nullptr;
	return PlayerController && PlayerController->IsLocalController()
		? PlayerController
		: nullptr;
}
