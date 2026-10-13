#include "Tutorial/Actors/Tuto_CombatObserver.h"

#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Room/RoomVolume.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_CombatObserver::ATuto_CombatObserver()
{
	EventTriggers.SetNum(3);
	// Keep a runtime root in cooked builds, where AInfo's editor sprite is absent.
	USceneComponent* GuideRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuideRoot"));
	SetRootComponent(GuideRoot);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GuideRoot);
	// Observation stays server-only; clients need the target anchor and shared destruction.
	bNetLoadOnClient = true;
	bReplicates = true;
	SetActorHiddenInGame(false);
}

void ATuto_CombatObserver::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client) return;
	if (!RoomTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s has no RoomTag; combat events are not observed."), *GetName());
		return;
	}
	if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
	{
		ObservedCombat = Combat;
		Combat->OnCombatEvent.AddDynamic(this, &ThisClass::HandleCombatEvent);
	}
	if (UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>())
	{
		ObservedAdaptation = Adaptation;
		Adaptation->OnAdaptationUpdated.AddUObject(this, &ThisClass::HandleAdaptationUpdated);
	}
}

void ATuto_CombatObserver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopObserving();
	Super::EndPlay(EndPlayReason);
}

void ATuto_CombatObserver::HandleReinforcement(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(ReinforcementSlot, SourcePlayerState);
}

void ATuto_CombatObserver::HandleFirstResistance(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(ResistanceSlot, SourcePlayerState);
}

void ATuto_CombatObserver::HandleRoomCleared(AOutlierPlayerState* SourcePlayerState)
{
	TriggerEvent(ClearedSlot, SourcePlayerState);
}

void ATuto_CombatObserver::HandleCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event,
	int32 CombatPhaseIndex, int32 GameplayGeneration)
{
	if (EventRoomTag != RoomTag || Event != ERoomCombatEvent::RoomCleared) return;
	if (AOutlierPlayerState* Source = FindPlayerInRoom()) HandleRoomCleared(Source);
	// Reinforcement and resistance cannot happen in a cleared room any more, so the observer is finished.
	StopObserving();
	Destroy();
}

void ATuto_CombatObserver::HandleAdaptationUpdated(const FEnemyAdaptationUpdateResult& Result)
{
	const URoomCombatSubsystem* Combat = ObservedCombat.Get();
	// Save/checkpoint restores report Ignore, so only accumulated gun kills crossing into resistance count.
	// Repeats after a break are rejected by the once-per-pair slot record.
	if (Result.KillCategory != EEnemyFinalKillCategory::Gun
		|| Result.PreviousState >= EEnemyAdaptationState::ResistanceLevel1
		|| Result.CurrentState < EEnemyAdaptationState::ResistanceLevel1
		|| !Combat || Combat->GetActiveCombatRoomTag() != RoomTag) return;
	if (AOutlierPlayerState* Source = FindPlayerInRoom()) HandleFirstResistance(Source);
}

AOutlierPlayerState* ATuto_CombatObserver::FindPlayerInRoom() const
{
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const ARoomVolume* Room = nullptr;
	if (GameState)
	{
		for (TActorIterator<ARoomVolume> It(World); It && !Room; ++It)
			if (IsValid(*It) && It->GetRoomTag() == RoomTag) Room = *It;
	}
	if (Room)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			AOutlierPlayerState* State = Cast<AOutlierPlayerState>(PlayerState);
			// The controlled pawn is used, so a partner possessing an enemy is located by that enemy.
			const APawn* Pawn = IsValid(State) ? State->GetPawn() : nullptr;
			if (IsValid(Pawn) && State->GetPairId() != INDEX_NONE && Room->ContainsWorldLocation(Pawn->GetActorLocation()))
				return State;
		}
	}
	UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s found no paired player inside room '%s'."),
		*GetName(), *RoomTag.ToString());
	return nullptr;
}

void ATuto_CombatObserver::StopObserving()
{
	if (URoomCombatSubsystem* Combat = ObservedCombat.Get())
		Combat->OnCombatEvent.RemoveDynamic(this, &ThisClass::HandleCombatEvent);
	if (UEnemyAdaptationSubsystem* Adaptation = ObservedAdaptation.Get())
		Adaptation->OnAdaptationUpdated.RemoveAll(this);
	ObservedCombat.Reset();
	ObservedAdaptation.Reset();
}
