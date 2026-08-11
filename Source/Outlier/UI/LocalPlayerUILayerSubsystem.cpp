#include "UI/LocalPlayerUILayerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
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

	AFirstPersonPlayerController* PlayerController = GetLocalFirstPersonController();
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

	if (!LayerRootWidget->AddToPlayerScreen(1000))
	{
		LayerRootWidget->AddToViewport(1000);
	}

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

	AFirstPersonPlayerController* PlayerController = GetLocalFirstPersonController();
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
		Request.bShowCursor);
}

FUILayerHandle ULocalPlayerUILayerSubsystem::PushWidget(
	FGameplayTag LayerTag,
	UUserWidget* Widget,
	FGameplayTag InputModeTag,
	UObject* RequestOwner,
	EUILayerFocusTarget FocusTarget,
	bool bShowCursor)
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
	if (!AttachWidgetToLayer(LayerTag, Widget, Handle.Id))
	{
		return {};
	}

	FUILayerEntry& Entry = LayerEntries.AddDefaulted_GetRef();
	Entry.Handle = Handle;
	Entry.LayerTag = LayerTag;
	Entry.InputModeTag = InputModeTag;
	Entry.Widget = Widget;
	Entry.RequestOwner = RequestOwner;
	Entry.FocusTarget = FocusTarget;
	Entry.bShowCursor = bShowCursor;

	RefreshTopLayerInput();
	return Handle;
}

FUILayerHandle ULocalPlayerUILayerSubsystem::PushInputScope(
	FGameplayTag LayerTag,
	FGameplayTag InputModeTag,
	UObject* RequestOwner,
	EUILayerFocusTarget FocusTarget,
	bool bShowCursor)
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
	bool bShowCursor)
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
	if (!AttachWidgetToLayer(LayerTag, Widget, NewHandle.Id))
	{
		return {};
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
	Entry.RequestOwner = RequestOwner;
	Entry.FocusTarget = FocusTarget;
	Entry.bShowCursor = bShowCursor;

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

	LayerEntries.RemoveAt(Index);
	RefreshTopLayerInput();
	return true;
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
	UUserWidget* InputWidget = CachedInputWidget.Get();
	if (!IsValid(InputWidget)
		|| !InputWidget->GetClass()->ImplementsInterface(UUILayerInputReceiver::StaticClass()))
	{
		return false;
	}

	const FUILayerHandle RoutedLayerHandle = CachedTopLayerHandle;
	if (IUILayerInputReceiver::Execute_HandleUILayerEscape(InputWidget))
	{
		return true;
	}

	// An unhandled escape request closes only the layer that received the input.
	return PopLayer(RoutedLayerHandle);
}

bool ULocalPlayerUILayerSubsystem::RouteWidgetConfirmedInput()
{
	UUserWidget* InputWidget = CachedInputWidget.Get();
	if (!IsValid(InputWidget)
		|| !InputWidget->GetClass()->ImplementsInterface(UUILayerInputReceiver::StaticClass()))
	{
		return false;
	}

	return IUILayerInputReceiver::Execute_HandleUILayerConfirmed(InputWidget);
}

bool ULocalPlayerUILayerSubsystem::AttachWidgetToLayer(
	FGameplayTag LayerTag,
	UUserWidget* Widget,
	int32 ZOrder)
{
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
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetAlignment(FVector2D::ZeroVector);
			Slot->SetAutoSize(false);
			Slot->SetZOrder(ZOrder);
			return true;
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
	AFirstPersonPlayerController* PlayerController = GetLocalFirstPersonController();
	if (!PlayerController || !PlayerController->SetFirstPersonInputMode(Layer.InputModeTag))
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
	if (AFirstPersonPlayerController* PlayerController = GetLocalFirstPersonController())
	{
		PlayerController->RestoreFirstPersonDefaultInputMode();
	}
}

AFirstPersonPlayerController* ULocalPlayerUILayerSubsystem::GetLocalFirstPersonController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer && GetWorld()
		? LocalPlayer->GetPlayerController(GetWorld())
		: nullptr;
	AFirstPersonPlayerController* FirstPersonController =
		Cast<AFirstPersonPlayerController>(PlayerController);
	return FirstPersonController && FirstPersonController->IsLocalController()
		? FirstPersonController
		: nullptr;
}
