#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Actor.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/Trigger/Tuto_EventRelay.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"

bool ITuto_EventTriggerInterface::TriggerEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState)
{
	return DispatchEvent(EventIndex, SourcePlayerState, ETuto_EventOperation::Trigger, ETuto_EventRegion::Both);
}

bool ITuto_EventTriggerInterface::CompleteEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState, ETuto_EventRegion Region)
{
	return DispatchEvent(EventIndex, SourcePlayerState, ETuto_EventOperation::Complete, Region);
}

bool ITuto_EventTriggerInterface::CancelEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState, ETuto_EventRegion Region)
{
	return DispatchEvent(EventIndex, SourcePlayerState, ETuto_EventOperation::Cancel, Region);
}

bool ITuto_EventTriggerInterface::NotifyEventOverlap(int32 EventIndex, AOutlierPlayerState* SourcePlayerState, ETuto_EventRegion Region)
{
	return DispatchEvent(EventIndex, SourcePlayerState, ETuto_EventOperation::Overlap, Region);
}

bool ITuto_EventTriggerInterface::DispatchEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
	ETuto_EventOperation Operation, ETuto_EventRegion Region)
{
	const AActor* Actor = Cast<AActor>(_getUObject());
	if (!IsValid(Actor) || !Actor->HasAuthority() || bDispatchingEvent
		|| (Operation == ETuto_EventOperation::Trigger && SubmittedEventIndices.Contains(EventIndex))) return false;
	const FGameplayTag EventTag = GetEventTag(EventIndex);
	if (!EventTag.IsValid()) return false;
	TGuardValue<bool> DispatchGuard(bDispatchingEvent, true);
	// Mark before local delivery/callbacks. Failed resolution leaves the slot retryable.
	if (Operation == ETuto_EventOperation::Trigger) SubmittedEventIndices.Add(EventIndex);
	const bool bAccepted = DispatchEventTag(Actor->GetWorld(), EventTag, SourcePlayerState, Operation, Region, Actor->GetName());
	if (!bAccepted && Operation == ETuto_EventOperation::Trigger) SubmittedEventIndices.Remove(EventIndex);
	return bAccepted;
}

bool ITuto_EventTriggerInterface::DispatchEventTag(UWorld* World, FGameplayTag EventTag, AOutlierPlayerState* SourcePlayerState,
	ETuto_EventOperation Operation, ETuto_EventRegion Region, const FString& Source)
{
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client || !EventTag.IsValid()
		|| !IsValid(SourcePlayerState) || !SourcePlayerState->HasAuthority() || SourcePlayerState->GetWorld() != World)
		return false;
	if (Region != ETuto_EventRegion::Upper && Region != ETuto_EventRegion::Gif && Region != ETuto_EventRegion::Both
		&& Region != ETuto_EventRegion::WorldGuide && Region != ETuto_EventRegion::All) return false;

	const auto DispatchWorldGuide = [World, EventTag, Operation]()
	{
		AOutlierWorldGuideManager* Manager = AOutlierWorldGuideManager::Find(World);
		if (!Manager) return false;
		switch (Operation)
		{
		case ETuto_EventOperation::Trigger: return Manager->TriggerEvent(EventTag);
		case ETuto_EventOperation::Complete: return Manager->CompleteEvent(EventTag);
		case ETuto_EventOperation::Cancel: return Manager->CancelEvent(EventTag);
		case ETuto_EventOperation::Overlap: return Manager->NotifyEventOverlap(EventTag);
		default: return false;
		}
	};
	if (Region == ETuto_EventRegion::WorldGuide) return DispatchWorldGuide();

	TArray<AFirstPersonPlayerController*, TInlineAllocator<2>> Recipients;
	const auto AddRecipient = [&Recipients, SourcePlayerState, World](APawn* Pawn, EOutlierPlayerRole Role)
	{
		AOutlierPlayerState* State = IsValid(Pawn) ? Pawn->GetPlayerState<AOutlierPlayerState>() : nullptr;
		AFirstPersonPlayerController* Controller = IsValid(Pawn) ? Cast<AFirstPersonPlayerController>(Pawn->GetController()) : nullptr;
		// Pair identity only validates linked recipients; it never keys one-shot execution.
		if (!IsValid(State) || State->GetWorld() != World || State->GetPlayerRole() != Role
			|| State->GetPairId() != SourcePlayerState->GetPairId() || !IsValid(Controller)
			|| Controller->GetPawn() != Pawn || State->GetOwner() != Controller) return false;
		Recipients.AddUnique(Controller);
		return true;
	};
	if (!AddRecipient(SourcePlayerState->GetShooterCharacter(), EOutlierPlayerRole::Shooter)
		|| !AddRecipient(SourcePlayerState->GetPartnerCharacter(), EOutlierPlayerRole::Partner))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s could not resolve both recipients for '%s'."), *Source, *EventTag.ToString());
		return false;
	}
	TArray<ATuto_EventRelay*, TInlineAllocator<2>> Relays;
	for (AFirstPersonPlayerController* Recipient : Recipients)
	{
		ATuto_EventRelay* Relay = ATuto_EventRelay::FindOrCreate(Recipient);
		if (!Relay) return false;
		Relays.Add(Relay);
	}
	// Shared state changes once, even though presentation is delivered twice.
	if (Operation == ETuto_EventOperation::Trigger || Region == ETuto_EventRegion::All) DispatchWorldGuide();
	for (ATuto_EventRelay* Relay : Relays)
		Relay->QueueCommand(EventTag, Operation, Region == ETuto_EventRegion::All ? ETuto_EventRegion::Both : Region, Source);
	return true;
}
