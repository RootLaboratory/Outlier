#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Actor.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Tutorial/Trigger/Tuto_EventRelay.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
	// PIE는 창마다 별도 GameInstance를 가지므로, 기록은 서버 쪽에 있다.
	UOutlierSaveSubSystem* FindServerSaves(UWorld* World)
	{
		UWorld* ServerWorld = World && World->GetNetMode() != NM_Client ? World : nullptr;
		if (!ServerWorld && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate->WorldType == EWorldType::PIE && Candidate->GetNetMode() != NM_Client)
				{
					ServerWorld = Candidate;
					break;
				}
			}
		}
		UGameInstance* GameInstance = ServerWorld ? ServerWorld->GetGameInstance() : nullptr;
		UOutlierSaveSubSystem* Saves = GameInstance ? GameInstance->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
		if (!Saves) UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] No server SaveSubsystem in this process; run it on the host."));
		return Saves;
	}

	FAutoConsoleCommandWithWorldAndArgs FiredDumpCommand(TEXT("Outlier.Event.Fired.Dump"),
		TEXT("List tutorial EventTags already fired in this game flow (server record)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			const UOutlierSaveSubSystem* Saves = FindServerSaves(World);
			if (!Saves) return;
			UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] Fired tutorial events: %d"), Saves->GetFiredTutorialEvents().Num());
			for (const FGameplayTag& Tag : Saves->GetFiredTutorialEvents())
				UE_LOG(LogOutlier, Log, TEXT("[EventTrigger]   %s"), *Tag.ToString());
		}));

	FAutoConsoleCommandWithWorldAndArgs FiredClearCommand(TEXT("Outlier.Event.Fired.Clear"),
		TEXT("Clear [EventTag]: remove one tag, or every tag, from the server game-flow record. Actor slot records stay."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOutlierSaveSubSystem* Saves = FindServerSaves(World);
			if (!Saves) return;
			if (Args.IsEmpty())
			{
				const TArray<FGameplayTag> Tags = Saves->GetFiredTutorialEvents().Array();
				for (const FGameplayTag& Tag : Tags) Saves->UnmarkTutorialEventFired(Tag);
				UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] Cleared %d fired tutorial events."), Tags.Num());
				return;
			}
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
			Saves->UnmarkTutorialEventFired(Tag);
			UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] Cleared fired tutorial event '%s'."), *Tag.ToString());
		}));
}

void ITuto_EventTriggerInterface::ResetSubmittedEventsForDebug()
{
	const AActor* Actor = Cast<AActor>(_getUObject());
	UOutlierSaveSubSystem* Saves = Actor && Actor->GetGameInstance()
		? Actor->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	if (Saves)
		for (const int32 EventIndex : SubmittedEventIndices) Saves->UnmarkTutorialEventFired(GetEventTag(EventIndex));
	SubmittedEventIndices.Reset();
}
#endif

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
	const bool bTrigger = Operation == ETuto_EventOperation::Trigger;
	// 2차 확인: game-flow 기록은 이 액터를 다시 생성하는 sublevel 리로드와 체크포인트 복원에도 유지된다.
	UOutlierSaveSubSystem* Saves = bTrigger && Actor->GetGameInstance()
		? Actor->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	if (Saves && Saves->HasFiredTutorialEvent(EventTag))
	{
		SubmittedEventIndices.Add(EventIndex);
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s skipped '%s': already fired in this game flow."),
			*Actor->GetName(), *EventTag.ToString());
		return false;
	}
	TGuardValue<bool> DispatchGuard(bDispatchingEvent, true);
	// 로컬 전달/콜백 전에 표시한다. 확정에 실패하면 slot은 재시도 가능 상태로 남는다.
	if (bTrigger) SubmittedEventIndices.Add(EventIndex);
	if (Saves) Saves->MarkTutorialEventFired(EventTag);
	const bool bAccepted = DispatchEventTag(Actor->GetWorld(), EventTag, SourcePlayerState, Operation, Region, Actor->GetName());
	if (!bAccepted && bTrigger)
	{
		SubmittedEventIndices.Remove(EventIndex);
		if (Saves) Saves->UnmarkTutorialEventFired(EventTag);
	}
	if (bTrigger)
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s trigger '%s' -> %s (game-flow record %s)"), *Actor->GetName(),
			*EventTag.ToString(), bAccepted ? TEXT("sent") : TEXT("failed, retryable"), Saves ? TEXT("on") : TEXT("missing"));
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
		// Pair 식별자는 연결된 수신자 검증에만 쓰며, one-shot 실행의 키로 쓰지 않는다.
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
	// 연출은 두 번 전달되더라도 공유 상태는 한 번만 바뀐다.
	if (Operation == ETuto_EventOperation::Trigger || Region == ETuto_EventRegion::All) DispatchWorldGuide();
	for (ATuto_EventRelay* Relay : Relays)
		Relay->QueueCommand(EventTag, Operation, Region == ETuto_EventRegion::All ? ETuto_EventRegion::Both : Region, Source);
	return true;
}
