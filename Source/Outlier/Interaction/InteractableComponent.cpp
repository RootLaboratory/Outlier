// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractableComponent.h"

#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Drone/Partner/HackableComponent.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/InteractionDescActor.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "UI/InteractInfoWidget.h"
#include "UI/InteractKeyWidget.h"
#include "UI/InteractionLockedWidget.h"
#include "UI/InteractionOutlineWidget.h"
#include "UObject/ConstructorHelpers.h"

namespace InteractableComponentTags
{
	static FGameplayTag Hold()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Type.Hold")), false);
	}

	static FGameplayTag Concurrent()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Session.Concurrent")), false);
	}

	static FGameplayTag MultipleUse()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Use.Multiple")), false);
	}

	static FGameplayTag Used()
	{
		return OutlierGameplayTags::State::Used();
	}

	static FGameplayTag Locked()
	{
		return OutlierGameplayTags::State::Locked();
	}

	static FGameplayTag Disabled()
	{
		return OutlierGameplayTags::State::Disabled();
	}
}

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
	HoldSessions.Reserve(MaxConcurrentHoldSessions);

	static ConstructorHelpers::FClassFinder<UInteractKeyWidget> KeyWidget(
		TEXT("/Game/Blueprints/Widget/FirstPerson/Interaction/WBP_InteractionKey"));
	InteractKeyWidgetClass = KeyWidget.Class;

	static ConstructorHelpers::FClassFinder<UInteractionLockedWidget> LockedWidget(
		TEXT("/Game/Blueprints/Widget/FirstPerson/Interaction/WBP_InteractionLockedWidget"));
	InteractionLockedWidgetClass = LockedWidget.Class;

	static ConstructorHelpers::FClassFinder<UInteractionOutlineWidget> OutlineWidget(
		TEXT("/Game/Blueprints/Widget/FirstPerson/Interaction/WBP_UInteractionOutlineWidget"));
	InteractionOutlineWidgetClass = OutlineWidget.Class;
}

void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->RegisterWidgetOwner(this);
	}
}

void UInteractableComponent::OnOutlineRangeChanged_Implementation(
	AFirstPersonCharacter* Interactor, bool bInRange)
{
	// Outline and widget presentation can be implemented here or in a Blueprint override.
}

void UInteractableComponent::DismissActorBasedWidgets_Implementation()
{
	InteractInfoWidgetDeactivate();
	InteractKeyWidgetDeactivate();
	InteractionLockedWidgetDeactivate();
	InteractionOutlineWidgetDeactivate();
}

void UInteractableComponent::RefreshWidgetTick()
{
	const auto IsShown = [](const UWidgetComponent* Component)
	{
		return IsValid(Component) && Component->IsVisible();
	};
	SetComponentTickEnabled(!HoldSessions.IsEmpty()
		|| IsShown(InteractInfoWidgetComponent) || IsShown(InteractKeyWidgetComponent)
		|| IsShown(InteractionLockedWidgetComponent) || IsShown(InteractionOutlineWidgetComponent));
}

void UInteractableComponent::UpdateWidgetPresentation()
{
	const auto Apply = [this](UWidgetComponent* Component, const FWorldWidgetPresentation& Settings, float ZOffset)
	{
		if (!IsValid(Component) || !Component->IsVisible() || !GetOwner()) return;
		const FVector Anchor = Component->GetAttachParent()
			? Component->GetAttachParent()->GetComponentLocation() : GetOwner()->GetActorLocation();
		Settings.Apply(Component, Anchor + FVector::UpVector * ZOffset);
	};
	Apply(InteractInfoWidgetComponent, InteractInfoWidgetPresentation, InteractInfoWidgetZOffset);
	Apply(InteractKeyWidgetComponent, InteractKeyWidgetPresentation, InteractKeyWidgetZOffset);
	Apply(InteractionLockedWidgetComponent, InteractionLockedWidgetPresentation, InteractionLockedWidgetZOffset);
	Apply(InteractionOutlineWidgetComponent, InteractionOutlineWidgetPresentation, InteractionOutlineWidgetZOffset);
	RefreshWidgetTick();
}

void UInteractableComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickHoldInteractions(DeltaTime);
	UpdateWidgetPresentation();
}

void UInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->UnregisterWidgetOwner(this);
	}
	CancelAllHoldSessions();
	ClearInteractionDescActor();
	InteractionLockedWidgetDeactivate();
	InteractionOutlineWidgetDeactivate();
	Super::EndPlay(EndPlayReason);
}

void UInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInteractableComponent, InteractableTags);
}

bool UInteractableComponent::IsLockedYet() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner)
		&& Owner->FindComponentByClass<UHackableComponent>()
		&& InteractableTags.HasTagExact(InteractableComponentTags::Locked());
}

bool UInteractableComponent::CanInteract(const FGameplayTagContainer& InteractorTags) const
{
	return MeetsInteractionRequirements(InteractorTags)
		&& !InteractableTags.HasTagExact(InteractableComponentTags::Locked());
}

bool UInteractableComponent::CanShowInteractionWidgets(const FGameplayTagContainer& InteractorTags) const
{
	return MeetsInteractionRequirements(InteractorTags)
		&& (!InteractableTags.HasTagExact(InteractableComponentTags::Locked()) || IsLockedYet());
}

bool UInteractableComponent::MeetsInteractionRequirements(const FGameplayTagContainer& InteractorTags) const
{
	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	if (UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
	{
		return false;
	}

	const FGameplayTag DisabledTag = InteractableComponentTags::Disabled();
	if (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag))
	{
		return false;
	}

	const FGameplayTag ShooterRoleTag = OutlierGameplayTags::Actor::Role::Shooter();
	if (ShooterRoleTag.IsValid()
		&& InteractableTags.HasTagExact(ShooterRoleTag)
		&& !InteractorTags.HasTagExact(ShooterRoleTag))
	{
		return false;
	}

	if (!RequiredInteractorQuery.IsEmpty()
		&& !RequiredInteractorQuery.Matches(InteractorTags))
	{
		return false;
	}

	if (BlockedInteractorTags.Num() > 0
		&& InteractorTags.HasAny(BlockedInteractorTags))
	{
		return false;
	}

	return true;
}

bool UInteractableComponent::RequiresHoldInteract() const
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	return HoldTag.IsValid() && InteractableTags.HasTagExact(HoldTag);
}

bool UInteractableComponent::AllowsConcurrentHold() const
{
	const FGameplayTag ConcurrentTag = InteractableComponentTags::Concurrent();
	return ConcurrentTag.IsValid() && InteractableTags.HasTagExact(ConcurrentTag);
}

bool UInteractableComponent::IsMultipleUse() const
{
	const FGameplayTag MultipleUseTag = InteractableComponentTags::MultipleUse();
	return MultipleUseTag.IsValid() && InteractableTags.HasTagExact(MultipleUseTag);
}

EInteractionFlowResult UInteractableComponent::AdvanceInteractionFlow(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return EInteractionFlowResult::Rejected;
	}

	if (RequiresHoldInteract())
	{
		return EInteractionFlowResult::Completed;
	}

	MarkHoldReady();
	return EInteractionFlowResult::HoldReady;
}

bool UInteractableComponent::BeginHoldInteract(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !RequiresHoldInteract()
		|| !CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}

	if (FindHoldSessionIndex(Interactor) != INDEX_NONE)
	{
		return true;
	}

	if ((!AllowsConcurrentHold() && !HoldSessions.IsEmpty())
		|| HoldSessions.Num() >= MaxConcurrentHoldSessions)
	{
		return false;
	}

	FHoldInteractionSession& NewSession = HoldSessions.AddDefaulted_GetRef();
	NewSession.Interactor = Interactor;

	if (Interactor->IsLocallyControlled())
	{
		if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
		{
			const AActor* Owner = GetOwner();
			DescActor->SetActorLocation(
				(Owner ? Owner->GetActorLocation() : FVector::ZeroVector)
				+ FVector::UpVector * InteractionDescActorZOffset);
			DescActor->ActivateDescFromSource(Interactor, this);
			DescActor->SetProgress(0.0f);
		}
	}

	SetComponentTickEnabled(true);
	return true;
}

void UInteractableComponent::EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled)
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex == INDEX_NONE)
	{
		return;
	}

	const bool bWasLocalSession = Interactor && Interactor->IsLocallyControlled();
	HoldSessions.RemoveAt(SessionIndex);

	if (bCanceled && HoldSessions.IsEmpty())
	{

		if ((GetOwner() && GetOwner()->HasAuthority()) || !AllowsConcurrentHold())
		{
			ClearHoldReady();
		}
	}

	if (bWasLocalSession && IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}

	if (HoldSessions.IsEmpty())
	{
		RefreshWidgetTick();
	}
}

void UInteractableComponent::ResetHoldInteraction(AFirstPersonCharacter* Interactor)
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex != INDEX_NONE)
	{
		EndHoldInteract(Interactor, true);
		return;
	}

	if ((GetOwner() && GetOwner()->HasAuthority()) || !AllowsConcurrentHold())
	{
		ClearHoldReady();
	}

	if (Interactor && Interactor->IsLocallyControlled() && IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}
}

bool UInteractableComponent::CanCommitHoldInteraction(const AFirstPersonCharacter* Interactor) const
{
	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	if (SessionIndex == INDEX_NONE)
	{
		return false;
	}

	constexpr float CompletionToleranceSeconds = 0.1f;
	return HoldSessions[SessionIndex].ElapsedTime + CompletionToleranceSeconds >= HoldDuration;
}

bool UInteractableComponent::CommitHoldInteraction(AFirstPersonCharacter* Interactor)
{
	if (!CanCommitHoldInteraction(Interactor))
	{
		return false;
	}

	const int32 SessionIndex = FindHoldSessionIndex(Interactor);
	HoldSessions.RemoveAt(SessionIndex);

	CommitInteraction(Interactor);
	if (HoldSessions.IsEmpty()) RefreshWidgetTick();
	return true;
}

void UInteractableComponent::CommitInteraction(AFirstPersonCharacter* Interactor)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || IsMultipleUse()) return;
	MarkUsed(Interactor);
	OnRep_InteractableTags();
	Owner->ForceNetUpdate();
}

void UInteractableComponent::SyncInteractionStateFromServer(
	EInteractionFlowResult FlowResult)
{
	if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		MarkHoldReady();
		return;
	}

	if (FlowResult == EInteractionFlowResult::Completed
		&& !IsMultipleUse())
	{
		MarkUsed(nullptr);
	}
}

void UInteractableComponent::HandleInteractionSucceeded(
	AFirstPersonCharacter* Interactor,
	EInteractionFlowResult FlowResult,
	bool bSyncStateFromServer)
{
	InteractKeyWidgetDeactivate();

	if (bSyncStateFromServer)
	{
		SyncInteractionStateFromServer(FlowResult);
	}

	if (FlowResult == EInteractionFlowResult::Completed)
	{
		DeactivateInteractionDesc();
	}
	else if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		ActivateInteractionDesc(Interactor);
	}
}

void UInteractableComponent::MarkHoldReady()
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	if (HoldTag.IsValid())
	{
		InteractableTags.AddTag(HoldTag);
	}
}

void UInteractableComponent::ActivateInteractionDesc(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		DeactivateInteractionDesc();
		return;
	}

	if (AInteractionDescActor* DescActor = EnsureInteractionDescActor())
	{
		const AActor* Owner = GetOwner();
		DescActor->SetActorLocation(
			(Owner ? Owner->GetActorLocation() : FVector::ZeroVector)
			+ FVector::UpVector * InteractionDescActorZOffset);
		DescActor->ActivateDescFromSource(Interactor, this);
		DescActor->PopupAnimationCall(false);
		return;
	}

	InteractInfoWidgetActivate(Interactor);
}

void UInteractableComponent::DeactivateInteractionDesc()
{
	ClearInteractionDescActor();
	InteractInfoWidgetDeactivate();
}

void UInteractableComponent::InteractInfoWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractInfoWidgetDeactivate();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractInfoWidgetClass)
	{
		return;
	}

	UWidgetComponent* WidgetComponent = EnsureInteractInfoWidgetComponent(PlayerController);
	if (!WidgetComponent)
	{
		return;
	}

	FGameplayTag InteractTag;
	FInteractInfoRow InteractInfo;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UInteractInfoSubsystem* InteractInfoSubsystem =
		GameInstance ? GameInstance->GetSubsystem<UInteractInfoSubsystem>() : nullptr;

	if (!GetPrimaryInteractTag(InteractTag)
		|| !InteractInfoSubsystem
		|| !InteractInfoSubsystem->TryGetInteractInfo(InteractTag, InteractInfo))
	{
		return;
	}

	InteractKeyWidgetDeactivate();
	WidgetComponent->SetVisibility(true);
	WidgetComponent->SetWorldLocation(
		WidgetComponent->GetAttachParent()
			? WidgetComponent->GetAttachParent()->GetComponentLocation() + FVector::UpVector * InteractInfoWidgetZOffset
			: GetOwner()->GetActorLocation() + FVector::UpVector * InteractInfoWidgetZOffset);

	if (!InteractInfoWidget)
	{
		InteractInfoWidget = Cast<UInteractInfoWidget>(WidgetComponent->GetUserWidgetObject());
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->UpdateInteractInfo(InteractTag, InteractInfo);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractInfoWidgetDeactivate()
{
	if (InteractInfoWidgetComponent)
	{
		InteractInfoWidgetComponent->SetVisibility(false);
	}

	if (InteractInfoWidget)
	{
		InteractInfoWidget->ClearInteractInfo();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractKeyWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !Interactor->IsLocallyControlled())
	{
		return;
	}

	if (!CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractKeyWidgetDeactivate();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController || !InteractKeyWidgetClass)
	{
		return;
	}

	UWidgetComponent* WidgetComponent = EnsureInteractKeyWidgetComponent(PlayerController);
	if (!WidgetComponent)
	{
		return;
	}

	WidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, InteractKeyWidgetZOffset));
	WidgetComponent->SetVisibility(true);
	WidgetComponent->SetWorldLocation(
		WidgetComponent->GetAttachParent()
			? WidgetComponent->GetAttachParent()->GetComponentLocation() + FVector::UpVector * InteractKeyWidgetZOffset
			: GetOwner()->GetActorLocation() + FVector::UpVector * InteractKeyWidgetZOffset);

	if (!InteractKeyWidget)
	{
		InteractKeyWidget = Cast<UInteractKeyWidget>(WidgetComponent->GetUserWidgetObject());
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->UpdateInteractKey();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractKeyWidgetDeactivate()
{
	if (InteractKeyWidgetComponent)
	{
		InteractKeyWidgetComponent->SetVisibility(false);
	}

	if (InteractKeyWidget)
	{
		InteractKeyWidget->ClearInteractKey();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::UpdateInnerRangeWidgets(AFirstPersonCharacter* Interactor, bool bFocused)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled())
	{
		return;
	}

	InteractionOutlineWidgetDeactivate();
	if (!CanShowInteractionWidgets(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetDeactivate();
		DeactivateInteractionDesc();
		return;
	}
	if (CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractionLockedWidgetDeactivate();
		if (bFocused)
		{
			InteractKeyWidgetDeactivate();
		}
		else
		{
			InteractKeyWidgetActivate(Interactor);
		}
	}
	else if (IsLockedYet())
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetActivate(Interactor);
	}
	else
	{
		InteractKeyWidgetDeactivate();
		InteractionLockedWidgetDeactivate();
	}
}

void UInteractableComponent::InteractionLockedWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled()
		|| !CanShowInteractionWidgets(Interactor->GetOwnedGameplayTagsForQuery()) || !IsLockedYet())
	{
		InteractionLockedWidgetDeactivate();
		return;
	}
	if (!InteractionLockedWidgetComponent)
	{
		InteractionLockedWidgetComponent = CreateRangeWidgetComponent(
			Cast<APlayerController>(Interactor->GetController()), InteractionLockedWidgetClass,
			TEXT("InteractionLockedWidgetComponent"), InteractionLockedWidgetDrawSize, InteractionLockedWidgetZOffset);
		if (InteractionLockedWidgetComponent)
		{
			InteractionLockedWidget = Cast<UInteractionLockedWidget>(InteractionLockedWidgetComponent->GetUserWidgetObject());
		}
	}
	if (InteractionLockedWidgetComponent && InteractionLockedWidget)
	{
		InteractionLockedWidget->ShowLockedPrompt();
		InteractionLockedWidgetComponent->SetVisibility(true);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractionLockedWidgetDeactivate()
{
	if (InteractionLockedWidgetComponent)
	{
		InteractionLockedWidgetComponent->SetVisibility(false);
	}
	if (InteractionLockedWidget)
	{
		InteractionLockedWidget->HideLockedPrompt();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractionOutlineWidgetActivate(AFirstPersonCharacter* Interactor)
{
	if (!IsValid(Interactor) || !Interactor->IsLocallyControlled())
	{
		return;
	}
	const bool bCanInteract = CanInteract(Interactor->GetOwnedGameplayTagsForQuery());
	const bool bLocked = !bCanInteract && IsLockedYet();
	if (!CanShowInteractionWidgets(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		InteractionOutlineWidgetDeactivate();
		DeactivateInteractionDesc();
		return;
	}
	InteractKeyWidgetDeactivate();
	InteractionLockedWidgetDeactivate();
	if (!InteractionOutlineWidgetComponent)
	{
		InteractionOutlineWidgetComponent = CreateRangeWidgetComponent(
			Cast<APlayerController>(Interactor->GetController()), InteractionOutlineWidgetClass,
			TEXT("InteractionOutlineWidgetComponent"), InteractionOutlineWidgetDrawSize, InteractionOutlineWidgetZOffset);
		if (InteractionOutlineWidgetComponent)
		{
			InteractionOutlineWidget = Cast<UInteractionOutlineWidget>(InteractionOutlineWidgetComponent->GetUserWidgetObject());
		}
	}
	if (InteractionOutlineWidgetComponent && InteractionOutlineWidget)
	{
		InteractionOutlineWidget->SetLocked(bLocked);
		InteractionOutlineWidgetComponent->SetVisibility(true);
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::InteractionOutlineWidgetDeactivate()
{
	if (InteractionOutlineWidgetComponent)
	{
		InteractionOutlineWidgetComponent->SetVisibility(false);
	}
	if (InteractionOutlineWidget)
	{
		InteractionOutlineWidget->HideIndicator();
	}
	UpdateWidgetPresentation();
}

void UInteractableComponent::OnRep_InteractableTags()
{
	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	const FGameplayTag LockedTag = InteractableComponentTags::Locked();
	const FGameplayTag DisabledTag = InteractableComponentTags::Disabled();
	const bool bInteractionUnavailable =
		(UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
		|| (LockedTag.IsValid() && InteractableTags.HasTagExact(LockedTag))
		|| (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag));

	if (bInteractionUnavailable)
	{
		CancelAllHoldSessions();
		InteractKeyWidgetDeactivate();
		DeactivateInteractionDesc();
	}
	if ((UsedTag.IsValid() && InteractableTags.HasTagExact(UsedTag))
		|| (DisabledTag.IsValid() && InteractableTags.HasTagExact(DisabledTag)))
	{
		InteractionLockedWidgetDeactivate();
		InteractionOutlineWidgetDeactivate();
	}
}

int32 UInteractableComponent::FindHoldSessionIndex(const AFirstPersonCharacter* Interactor) const
{
	return HoldSessions.IndexOfByPredicate(
		[Interactor](const FHoldInteractionSession& Session)
		{
			return Session.Interactor.Get() == Interactor;
		});
}

void UInteractableComponent::TickHoldInteractions(float DeltaTime)
{
	for (int32 Index = HoldSessions.Num() - 1; Index >= 0; --Index)
	{
		FHoldInteractionSession& Session = HoldSessions[Index];
		AFirstPersonCharacter* Interactor = Session.Interactor.Get();
		if (!Interactor)
		{
			HoldSessions.RemoveAtSwap(Index);
			continue;
		}

		Session.ElapsedTime += DeltaTime;
		const float HoldProgress = FMath::Clamp(Session.ElapsedTime / FMath::Max(HoldDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

		if (Interactor->IsLocallyControlled() && IsValid(InteractionDescActor))
		{
			InteractionDescActor->SetProgress(HoldProgress);
		}

		if (HoldProgress >= 1.0f
			&& !Session.bCompletionNotified
			&& Interactor->IsLocallyControlled())
		{
			Session.bCompletionNotified = true;
			Interactor->NotifyHoldInteractCompleted(GetOwner());
		}
	}

	if (HoldSessions.IsEmpty())
	{
		RefreshWidgetTick();
	}
}

void UInteractableComponent::CancelOtherHoldSessions(AFirstPersonCharacter* CompletedInteractor)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (const FHoldInteractionSession& Session : HoldSessions)
		{
			if (AFirstPersonCharacter* Interactor = Session.Interactor.Get())
			{
				if (Interactor != CompletedInteractor)
				{
					Interactor->NotifyHoldInteractInvalidated(GetOwner());
				}
			}
		}
	}

	HoldSessions.Reset();
	RefreshWidgetTick();
}

void UInteractableComponent::CancelAllHoldSessions()
{
	HoldSessions.Reset();
	RefreshWidgetTick();

	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->SetProgress(0.0f);
		InteractionDescActor->DeactivateDesc();
	}

	InteractInfoWidgetDeactivate();
}

void UInteractableComponent::MarkUsed(AFirstPersonCharacter* CompletedInteractor)
{
	CancelOtherHoldSessions(CompletedInteractor);

	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();

	if (UsedTag.IsValid())
	{
		InteractableTags.AddTag(UsedTag);
	}
	if (HoldTag.IsValid())
	{
		InteractableTags.RemoveTag(HoldTag);
	}
}

void UInteractableComponent::RestoreUsedState(bool bUsed)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const FGameplayTag UsedTag = InteractableComponentTags::Used();
	if (!UsedTag.IsValid())
	{
		return;
	}

	if (bUsed)
	{
		CancelAllHoldSessions();
		InteractableTags.AddTag(UsedTag);
	}
	else
	{
		InteractableTags.RemoveTag(UsedTag);
	}
	Owner->ForceNetUpdate();
}

void UInteractableComponent::ClearHoldReady()
{
	const FGameplayTag HoldTag = InteractableComponentTags::Hold();
	if (HoldTag.IsValid())
	{
		InteractableTags.RemoveTag(HoldTag);
	}
}

AInteractionDescActor* UInteractableComponent::EnsureInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		return InteractionDescActor;
	}

	AActor* Owner = GetOwner();
	if (!InteractionDescActorClass || !Owner || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	InteractionDescActor = GetWorld()->SpawnActor<AInteractionDescActor>(
		InteractionDescActorClass,
		Owner->GetActorLocation() + FVector::UpVector * InteractionDescActorZOffset,
		Owner->GetActorRotation(),
		SpawnParams);

	if (InteractionDescActor)
	{
		InteractionDescActor->SetReplicates(false);
		InteractionDescActor->SetReplicateMovement(false);
	}

	return InteractionDescActor;
}

void UInteractableComponent::ClearInteractionDescActor()
{
	if (IsValid(InteractionDescActor))
	{
		InteractionDescActor->Destroy();
	}

	InteractionDescActor = nullptr;
}

UWidgetComponent* UInteractableComponent::EnsureInteractInfoWidgetComponent(APlayerController* PlayerController)
{
	if (InteractInfoWidgetComponent)
	{
		return InteractInfoWidgetComponent;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !PlayerController || !InteractInfoWidgetClass)
	{
		return nullptr;
	}

	UWidgetComponent* NewWidgetComponent = NewObject<UWidgetComponent>(Owner, TEXT("InteractInfoWidgetComponent"));
	if (!NewWidgetComponent)
	{
		return nullptr;
	}

	NewWidgetComponent->SetWidgetClass(InteractInfoWidgetClass);
	NewWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	NewWidgetComponent->SetDrawSize(InteractInfoWidgetDrawSize);
	NewWidgetComponent->SetDrawAtDesiredSize(false);
	NewWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewWidgetComponent->SetGenerateOverlapEvents(false);
	NewWidgetComponent->SetIsReplicated(false);
	NewWidgetComponent->SetVisibility(false);

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		NewWidgetComponent->SetOwnerPlayer(LocalPlayer);
	}

	if (USceneComponent* OwnerRootComponent = Owner->GetRootComponent())
	{
		NewWidgetComponent->SetupAttachment(OwnerRootComponent);
	}

	Owner->AddInstanceComponent(NewWidgetComponent);
	NewWidgetComponent->RegisterComponent();
	NewWidgetComponent->InitWidget();

	InteractInfoWidgetComponent = NewWidgetComponent;
	InteractInfoWidget = Cast<UInteractInfoWidget>(NewWidgetComponent->GetUserWidgetObject());
	return InteractInfoWidgetComponent;
}

UWidgetComponent* UInteractableComponent::EnsureInteractKeyWidgetComponent(APlayerController* PlayerController)
{
	if (InteractKeyWidgetComponent)
	{
		return InteractKeyWidgetComponent;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !PlayerController || !InteractKeyWidgetClass)
	{
		return nullptr;
	}

	UWidgetComponent* NewWidgetComponent = NewObject<UWidgetComponent>(Owner, TEXT("InteractKeyWidgetComponent"));
	if (!NewWidgetComponent)
	{
		return nullptr;
	}

	NewWidgetComponent->SetWidgetClass(InteractKeyWidgetClass);
	NewWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	NewWidgetComponent->SetDrawSize(InteractKeyWidgetDrawSize);
	NewWidgetComponent->SetDrawAtDesiredSize(false);
	NewWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, InteractKeyWidgetZOffset));
	NewWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewWidgetComponent->SetGenerateOverlapEvents(false);
	NewWidgetComponent->SetIsReplicated(false);
	NewWidgetComponent->SetVisibility(false);

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		NewWidgetComponent->SetOwnerPlayer(LocalPlayer);
	}

	if (USceneComponent* RootComponent = Owner->GetRootComponent())
	{
		NewWidgetComponent->SetupAttachment(RootComponent);
	}

	Owner->AddInstanceComponent(NewWidgetComponent);
	NewWidgetComponent->RegisterComponent();
	NewWidgetComponent->InitWidget();

	InteractKeyWidgetComponent = NewWidgetComponent;
	InteractKeyWidget = Cast<UInteractKeyWidget>(NewWidgetComponent->GetUserWidgetObject());
	return InteractKeyWidgetComponent;
}

UWidgetComponent* UInteractableComponent::CreateRangeWidgetComponent(
	APlayerController* PlayerController, TSubclassOf<UUserWidget> WidgetClass,
	FName Name, FVector2D DrawSize, float ZOffset)
{
	AActor* Owner = GetOwner();
	if (!Owner || !PlayerController || !WidgetClass)
	{
		return nullptr;
	}
	UWidgetComponent* Component = NewObject<UWidgetComponent>(Owner, Name);
	Component->SetWidgetClass(WidgetClass);
	Component->SetWidgetSpace(EWidgetSpace::Screen);
	Component->SetDrawSize(DrawSize);
	Component->SetDrawAtDesiredSize(false);
	Component->SetRelativeLocation(FVector(0.0f, 0.0f, ZOffset));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetIsReplicated(false);
	Component->SetVisibility(false);
	Component->SetOwnerPlayer(PlayerController->GetLocalPlayer());
	if (USceneComponent* Root = Owner->GetRootComponent())
	{
		Component->SetupAttachment(Root);
	}
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	Component->InitWidget();
	return Component;
}

bool UInteractableComponent::GetPrimaryInteractTag(FGameplayTag& OutInteractTag) const
{
	const FGameplayTag TargetRootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Target")), false);
	TArray<FGameplayTag> InteractableTagArray;
	InteractableTags.GetGameplayTagArray(InteractableTagArray);

	FGameplayTag FirstValidTag;
	for (const FGameplayTag& Tag : InteractableTagArray)
	{
		if (!Tag.IsValid())
		{
			continue;
		}

		if (!FirstValidTag.IsValid())
		{
			FirstValidTag = Tag;
		}

		if (TargetRootTag.IsValid() && Tag.MatchesTag(TargetRootTag))
		{
			OutInteractTag = Tag;
			return true;
		}
	}

	if (FirstValidTag.IsValid())
	{
		OutInteractTag = FirstValidTag;
		return true;
	}

	return false;
}
