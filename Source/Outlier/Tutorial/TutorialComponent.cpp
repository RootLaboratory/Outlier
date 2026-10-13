#include "Tutorial/TutorialComponent.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "Event/OutlierEventSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/PartnerTutorialComponent.h"
#include "Tutorial/ShooterTutorialComponent.h"

UTutorialComponent::UTutorialComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
	bAutoActivate = true;
}

UTutorialComponent* UTutorialComponent::AttachTutorial(APawn* Pawn)
{
	if (!IsValid(Pawn) || !Pawn->IsLocallyControlled())
	{
		return nullptr;
	}
	if (UTutorialComponent* Existing = Pawn->FindComponentByClass<UTutorialComponent>())
	{
		return Existing;
	}
	UClass* ComponentClass = Pawn->IsA<AShooterCharacter>() ? UShooterTutorialComponent::StaticClass()
		: Pawn->IsA<APartnerCharacter>() ? UPartnerTutorialComponent::StaticClass() : nullptr;
	if (!ComponentClass)
	{
		return nullptr;
	}
	UTutorialComponent* Component = NewObject<UTutorialComponent>(Pawn, ComponentClass);
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

bool UTutorialComponent::SendTriggerEvent(APawn* Pawn, FGameplayTag EventTag, const FString& Source)
{
	AFirstPersonPlayerController* Controller = IsValid(Pawn) && Pawn->IsLocallyControlled()
		? Cast<AFirstPersonPlayerController>(Pawn->GetController()) : nullptr;
	const bool bSent = Controller && Controller->RequestTutorialEvent(EventTag);
	if (bSent)
	{
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s sent '%s' for %s."),
			*Source, *EventTag.ToString(), *GetNameSafe(Pawn));
	}
	else
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s could not request '%s' for %s."),
			*Source, *EventTag.ToString(), *GetNameSafe(Pawn));
	}
	return bSent;
}

void UTutorialComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshInputBindings();
}

void UTutorialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearInputWatches();
	Super::EndPlay(EndPlayReason);
}

void UTutorialComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Watch input readiness/recreation here, without adding notifications to Character.
	APawn* Pawn = Cast<APawn>(GetOwner());
	UEnhancedInputComponent* Input = Pawn && GetEventSubsystem()
		? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (Input != BoundInputComponent.Get())
	{
		BindInput(Input);
	}
}

void UTutorialComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	RefreshInputBindings();
}

void UTutorialComponent::Deactivate()
{
	SetComponentTickEnabled(false);
	UnbindInput();
	Super::Deactivate();
}

UOutlierEventSubsystem* UTutorialComponent::GetEventSubsystem() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	return Pawn && SupportsOwner(Pawn) && Pawn->IsLocallyControlled() && LocalPlayer
		? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
}

bool UTutorialComponent::SendTutorialEvent(FGameplayTag EventTag, const FOutlierEventContext& Context)
{
	UOutlierEventSubsystem* Events = IsActive() ? GetEventSubsystem() : nullptr;
	return Events && Events->SendEvent(EventTag, Context);
}

bool UTutorialComponent::ReceiveEvent(FGameplayTag EventTag)
{
	UOutlierEventSubsystem* Events = IsActive() ? GetEventSubsystem() : nullptr;
	if (!Events || !EventTag.IsValid())
	{
		return false;
	}
	FOutlierUpperData Upper;
	if (Events->TryGetEventUpperData(EventTag, Upper) && Upper.Type == EOutlierUpperInfoType::Input
		&& !Upper.InputDisplay.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Tutorial] '%s' input row '%s' has no valid input display definition."),
			*EventTag.ToString(), *Upper.UpperTag.ToString());
	}
	// Sheet rule: every upper row, key hints included, closes after its DT Duration so chains advance on time.
	FOutlierEventContext Context;
	Context.EndCondition = EOutlierEventEndCondition::Time;
	return Events->SendEvent(EventTag, Context);
}

bool UTutorialComponent::StartInputTutorial(FGameplayTag EventTag, ETriggerEvent TriggerEvent,
	const FOutlierEventContext& Context)
{
	UOutlierEventSubsystem* Events = IsActive() ? GetEventSubsystem() : nullptr;
	FOutlierUpperData Row;
	if (!Events || !Events->TryGetEventUpperData(EventTag, Row) || Row.Type != EOutlierUpperInfoType::Input
		|| !Row.InputDisplay.IsValid())
	{
		return false;
	}
	if (Context.EndCondition == EOutlierEventEndCondition::Time)
	{
		const float Duration = Context.Duration == 0.0f ? Row.Duration : Context.Duration;
		if (!FMath::IsFinite(Duration) || Duration <= 0.0f) return false;
	}
	// Manual uses input completion; Time is owned by the widget and Overlap waits for its trigger.
	UInputAction* CompletionAction = Row.InputDisplay.CompletionInputAction;
	if (!CompletionAction && Row.InputDisplay.Expression.Num() == 1)
	{
		CompletionAction = Row.InputDisplay.Expression[0].InputAction;
	}
	// A display '+' does not define a gameplay completion condition. Composite manual hints
	// must explicitly select an action rather than completing on an arbitrary operand.
	if (Context.EndCondition == EOutlierEventEndCondition::Manual
		&& !AddInputWatch(CompletionAction, TriggerEvent, EventTag, Context, true, true))
	{
		return false;
	}
	if (!Events->SendEvent(EventTag, Context))
	{
		UnbindInput();
		InputWatches.RemoveAll([EventTag](const FOutlierTutorialInputWatch& Watch)
			{ return Watch.EventTag == EventTag && Watch.bCompletesInputUI; });
		RefreshInputBindings();
		return false;
	}
	StartedInputEventSubsystem = Events;
	StartedInputEventTags.AddUnique(EventTag);
	return true;
}

bool UTutorialComponent::WatchInput(UInputAction* InputAction, ETriggerEvent TriggerEvent,
	FGameplayTag EventTag, const FOutlierEventContext& Context, bool bOnce)
{
	return AddInputWatch(InputAction, TriggerEvent, EventTag, Context, bOnce, false);
}

void UTutorialComponent::SetTutorialTags(const FGameplayTagContainer& Tags)
{
	TutorialTags = Tags;
}

bool UTutorialComponent::SetInputCondition(FGameplayTag EventTag, const FGameplayTagQuery& Condition)
{
	FOutlierTutorialInputWatch* Watch = InputWatches.FindByPredicate(
		[EventTag](const FOutlierTutorialInputWatch& Candidate)
		{
			return Candidate.EventTag == EventTag;
		});
	if (!Watch)
	{
		return false;
	}
	Watch->Condition = Condition;
	return true;
}

bool UTutorialComponent::AddInputWatch(UInputAction* InputAction, ETriggerEvent TriggerEvent,
	FGameplayTag EventTag, const FOutlierEventContext& Context, bool bOnce, bool bCompletesInputUI)
{
	if (!IsActive() || !GetEventSubsystem() || !IsValid(InputAction) || !EventTag.IsValid()
		|| TriggerEvent == ETriggerEvent::None)
	{
		return false;
	}
	// Replace the previous watch for this event rather than accumulating duplicate delegates.
	int32 Index = InputWatches.IndexOfByPredicate([EventTag](const FOutlierTutorialInputWatch& Watch)
	{
		return Watch.EventTag == EventTag;
	});
	if (Index == INDEX_NONE)
	{
		Index = InputWatches.AddDefaulted();
	}
	UnbindInput();
	FOutlierTutorialInputWatch& Watch = InputWatches[Index];
	Watch = FOutlierTutorialInputWatch();
	Watch.Revision = NextWatchRevision++;
	Watch.InputAction = InputAction;
	Watch.TriggerEvent = TriggerEvent;
	Watch.EventTag = EventTag;
	Watch.Context = Context;
	Watch.bOnce = bOnce;
	Watch.bCompletesInputUI = bCompletesInputUI;
	RefreshInputBindings();
	return true;
}

void UTutorialComponent::RefreshInputBindings()
{
	const bool bHasPendingInput = InputWatches.ContainsByPredicate(
		[](const FOutlierTutorialInputWatch& Watch) { return !Watch.bCompleted; });
	SetComponentTickEnabled(IsActive() && bHasPendingInput);
	if (!IsActive() || !bHasPendingInput)
	{
		UnbindInput();
		return;
	}
	APawn* Pawn = Cast<APawn>(GetOwner());
	BindInput(Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr);
}

void UTutorialComponent::BindInput(UEnhancedInputComponent* InputComponent)
{
	UnbindInput();
	if (!IsActive() || !InputComponent || !GetEventSubsystem())
	{
		return;
	}
	BoundInputComponent = InputComponent;
	for (int32 Index = 0; Index < InputWatches.Num(); ++Index)
	{
		FOutlierTutorialInputWatch& Watch = InputWatches[Index];
		if (!Watch.bCompleted && IsValid(Watch.InputAction))
		{
			Watch.BindingHandle = InputComponent->BindAction(Watch.InputAction, Watch.TriggerEvent,
				this, &ThisClass::HandleInput, Index, Watch.Revision).GetHandle();
		}
	}
}

void UTutorialComponent::UnbindInput()
{
	for (FOutlierTutorialInputWatch& Watch : InputWatches)
	{
		if (Watch.BindingHandle != 0)
		{
			if (UEnhancedInputComponent* Input = BoundInputComponent.Get())
			{
				Input->RemoveBindingByHandle(Watch.BindingHandle);
			}
			Watch.BindingHandle = 0;
		}
	}
	BoundInputComponent.Reset();
}

void UTutorialComponent::ClearInputWatches()
{
	SetComponentTickEnabled(false);
	UnbindInput();
	// Only cancel hints that this component actually started.
	if (UOutlierEventSubsystem* Events = StartedInputEventSubsystem.Get())
	{
		const TArray<FGameplayTag> Tags = MoveTemp(StartedInputEventTags);
		for (FGameplayTag Tag : Tags)
		{
			Events->CancelUpperEvent(Tag);
		}
	}
	StartedInputEventSubsystem.Reset();
	StartedInputEventTags.Reset();
	InputWatches.Reset();
}

void UTutorialComponent::HandleInput(const FInputActionInstance& Instance, int32 WatchIndex, uint32 Revision)
{
	if (!IsActive() || !InputWatches.IsValidIndex(WatchIndex) || InputWatches[WatchIndex].bCompleted
		|| InputWatches[WatchIndex].Revision != Revision
		|| InputWatches[WatchIndex].InputAction != Instance.GetSourceAction())
	{
		return;
	}
	UOutlierEventSubsystem* Events = GetEventSubsystem();
	if (!Events)
	{
		return;
	}
	const FGameplayTagQuery& Condition = InputWatches[WatchIndex].Condition;
	if (!Condition.IsEmpty() && !Condition.Matches(TutorialTags))
	{
		return;
	}
	// Copy before callbacks, which may clear watches or destroy this component.
	const FOutlierTutorialInputWatch Watch = InputWatches[WatchIndex];
	// A queued tutorial must not complete from input meant for the currently displayed one.
	if (Watch.bCompletesInputUI && Events->GetActiveInputEventTag() != Watch.EventTag)
	{
		return;
	}
	if (Watch.bOnce)
	{
		InputWatches[WatchIndex].bCompleted = true;
	}
	const bool bAccepted = Watch.bCompletesInputUI
		? Events->CompleteInputEvent(Watch.EventTag) : Events->SendEvent(Watch.EventTag, Watch.Context);
	if (!bAccepted && InputWatches.IsValidIndex(WatchIndex) && InputWatches[WatchIndex].Revision == Revision)
	{
		InputWatches[WatchIndex].bCompleted = false;
	}
	if (bAccepted && Watch.bCompletesInputUI)
	{
		OnInputTutorialCompleted.Broadcast(Watch.EventTag);
	}
	// Removing the binding during its execution is supported by Enhanced Input's delegate copy.
	RefreshInputBindings();
}

bool UShooterTutorialComponent::SupportsOwner(const APawn* Pawn) const
{
	return Pawn && Pawn->IsA<AShooterCharacter>();
}

bool UPartnerTutorialComponent::SupportsOwner(const APawn* Pawn) const
{
	return Pawn && Pawn->IsA<APartnerCharacter>();
}
