#include "Room/RoomCombatSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Subsystems/SubsystemCollection.h"

void URoomCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UOutlierArenaSubsystem* ArenaSubsystem =
		Collection.InitializeDependency<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->OnArenaReleased.AddUObject(
			this,
			&URoomCombatSubsystem::HandleArenaReleased);
	}
}

void URoomCombatSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UOutlierArenaSubsystem* ArenaSubsystem =
			World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			ArenaSubsystem->OnArenaReleased.RemoveAll(this);
		}
	}

	ResetRuntimeCombatState();
	Super::Deinitialize();
}

bool URoomCombatSubsystem::RegisterRoom(
	ARoomVolume* RoomVolume,
	FGameplayTag RoomTag,
	URoomCombatDefinition* Definition)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| !IsValid(RoomVolume) || !RoomTag.IsValid() || !Definition)
	{
		return false;
	}

	if (const FRoomCombatRuntime* Existing = RoomRuntimes.Find(RoomTag))
	{
		if (Existing->RoomVolume.Get() != RoomVolume)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomCombat] Duplicate RoomTag. RoomTag=%s Existing=%s New=%s"),
				*RoomTag.ToString(),
				*GetNameSafe(Existing->RoomVolume.Get()),
				*GetNameSafe(RoomVolume));
			return false;
		}
		return true;
	}

	FRoomCombatRuntime& Runtime = RoomRuntimes.Add(RoomTag);
	Runtime.RoomVolume = RoomVolume;
	Runtime.Definition = Definition;

	UGameInstance* GameInstance = World->GetGameInstance();
	UOutlierSaveSubSystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	const FName EncounterId = RoomTag.GetTagName();
	if (SaveSubsystem)
	{
		Runtime.bEncounterIdRegistered = SaveSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CompletedEncounter,
			EncounterId,
			RoomVolume);
	}

	if (Runtime.bEncounterIdRegistered
		&& SaveSubsystem->HasWorldProgress(
			EOutlierWorldProgressType::CompletedEncounter,
			EncounterId))
	{
		Runtime.State = ERoomCombatState::Cleared;
	}
	else if (!Definition->CombatPhases.IsEmpty()
		&& Definition->CombatPhases[0].StartPolicy
			== ERoomCombatPhaseStartPolicy::HackTrigger)
	{
		Runtime.State = ERoomCombatState::WaitingForTrigger;
	}

	if (TSet<TWeakObjectPtr<AEnemyBase>>* PendingEnemies =
		PendingPreplacedEnemies.Find(RoomTag))
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *PendingEnemies)
		{
			AEnemyBase* Enemy = EnemyPtr.Get();
			if (!IsValid(Enemy))
			{
				continue;
			}

			if (Runtime.State == ERoomCombatState::Cleared)
			{
				Enemy->Destroy();
				RegisteredEnemyRooms.Remove(EnemyPtr);
				continue;
			}

			Runtime.AlivePreplacedEnemies.Add(EnemyPtr);
			Runtime.bHadPreplacedEnemy = true;
		}
		PendingPreplacedEnemies.Remove(RoomTag);
	}

	return true;
}

void URoomCombatSubsystem::UnregisterRoom(ARoomVolume* RoomVolume)
{
	if (!RoomVolume)
	{
		return;
	}

	const FGameplayTag RoomTag = RoomVolume->GetRoomTag();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || Runtime->RoomVolume.Get() != RoomVolume)
	{
		return;
	}

	if (Runtime->bEncounterIdRegistered)
	{
		UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
			? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::CompletedEncounter,
				RoomTag.GetTagName(),
				RoomVolume);
		}
	}

	if (ActiveCombatRoomTag == RoomTag)
	{
		ActiveCombatRoomTag = FGameplayTag();
	}
	if (UWorld* World = GetWorld())
	{
		if (UEnemyRoomSubsystem* EnemyRoomSubsystem =
			World->GetSubsystem<UEnemyRoomSubsystem>())
		{
			EnemyRoomSubsystem->NotifyRoomCombatEnded(RoomTag);
		}
	}
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Runtime->AlivePreplacedEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
		}
	}
	RoomRuntimes.Remove(RoomTag);
}

void URoomCombatSubsystem::RegisterPreplacedEnemy(AEnemyBase* Enemy)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(Enemy)
		|| !Enemy->HasAuthority())
	{
		return;
	}

	const FGameplayTag RoomTag = Enemy->GetDefaultRoomTag();
	if (!RoomTag.IsValid())
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	RegisteredEnemyRooms.Add(EnemyPtr, RoomTag);

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
		return;
	}

	if (Runtime->State == ERoomCombatState::Cleared)
	{
		RegisteredEnemyRooms.Remove(EnemyPtr);
		Enemy->Destroy();
		return;
	}

	Runtime->AlivePreplacedEnemies.Add(EnemyPtr);
	Runtime->bHadPreplacedEnemy = true;
}

void URoomCombatSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	const FGameplayTag* RegisteredRoomTag = RegisteredEnemyRooms.Find(EnemyPtr);
	if (!RegisteredRoomTag)
	{
		return;
	}

	if (FRoomCombatRuntime* Runtime = RoomRuntimes.Find(*RegisteredRoomTag))
	{
		Runtime->AlivePreplacedEnemies.Remove(EnemyPtr);
	}
	if (TSet<TWeakObjectPtr<AEnemyBase>>* PendingEnemies =
		PendingPreplacedEnemies.Find(*RegisteredRoomTag))
	{
		PendingEnemies->Remove(EnemyPtr);
		if (PendingEnemies->IsEmpty())
		{
			PendingPreplacedEnemies.Remove(*RegisteredRoomTag);
		}
	}
	RegisteredEnemyRooms.Remove(EnemyPtr);
}

void URoomCombatSubsystem::NotifyEnemyDefeated(AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->HasAuthority())
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	const FGameplayTag* RegisteredRoomTag = RegisteredEnemyRooms.Find(EnemyPtr);
	if (!RegisteredRoomTag)
	{
		return;
	}

	const FGameplayTag RoomTag = *RegisteredRoomTag;
	RegisteredEnemyRooms.Remove(EnemyPtr);
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		if (TSet<TWeakObjectPtr<AEnemyBase>>* PendingEnemies =
			PendingPreplacedEnemies.Find(RoomTag))
		{
			PendingEnemies->Remove(EnemyPtr);
		}
		return;
	}

	Runtime->AlivePreplacedEnemies.Remove(EnemyPtr);
	CompactAliveEnemies(*Runtime);
	if (!Runtime->bHadPreplacedEnemy || !Runtime->AlivePreplacedEnemies.IsEmpty())
	{
		return;
	}

	if (Runtime->State == ERoomCombatState::Dormant)
	{
		// 발각 전에 배치 적을 모두 제거하면 아직 시작하지 않은 후속 Wave를 만들지 않는다.
		CompleteCurrentPhase(RoomTag, true);
		return;
	}

	if (Runtime->State != ERoomCombatState::Combat)
	{
		return;
	}

	URoomCombatDefinition* Definition = Runtime->Definition.Get();
	if (!Definition
		|| !Definition->CombatPhases.IsValidIndex(Runtime->CurrentCombatPhaseIndex))
	{
		return;
	}

	const FRoomCombatPhaseDefinition& Phase =
		Definition->CombatPhases[Runtime->CurrentCombatPhaseIndex];
	if (Phase.Waves.Num() == 1)
	{
		CompleteCurrentPhase(RoomTag, false);
	}
}

bool URoomCombatSubsystem::NotifyRoomCombatStarted(FGameplayTag RoomTag)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !RoomTag.IsValid())
	{
		return false;
	}

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		return false;
	}
	if (Runtime->State == ERoomCombatState::Combat)
	{
		return ActiveCombatRoomTag == RoomTag;
	}
	if (Runtime->State != ERoomCombatState::Dormant
		|| (ActiveCombatRoomTag.IsValid() && ActiveCombatRoomTag != RoomTag))
	{
		return false;
	}

	CompactAliveEnemies(*Runtime);
	if (Runtime->AlivePreplacedEnemies.IsEmpty())
	{
		return false;
	}

	URoomCombatDefinition* Definition = Runtime->Definition.Get();
	if (!Definition
		|| !Definition->CombatPhases.IsValidIndex(Runtime->CurrentCombatPhaseIndex)
		|| Definition->CombatPhases[Runtime->CurrentCombatPhaseIndex].StartPolicy
			!= ERoomCombatPhaseStartPolicy::InitialDetection)
	{
		return false;
	}

	Runtime->State = ERoomCombatState::Combat;
	Runtime->CurrentWaveIndex = 0;
	ActiveCombatRoomTag = RoomTag;
	return true;
}

void URoomCombatSubsystem::ResetRuntimeCombatState()
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		for (const TPair<FGameplayTag, FRoomCombatRuntime>& Entry : RoomRuntimes)
		{
			if (Entry.Value.bEncounterIdRegistered && Entry.Value.RoomVolume.IsValid())
			{
				SaveSubsystem->UnregisterWorldProgressId(
					EOutlierWorldProgressType::CompletedEncounter,
					Entry.Key.GetTagName(),
					Entry.Value.RoomVolume.Get());
			}
		}
	}

	RoomRuntimes.Reset();
	PendingPreplacedEnemies.Reset();
	RegisteredEnemyRooms.Reset();
	ActiveCombatRoomTag = FGameplayTag();
}

bool URoomCombatSubsystem::IsRoomRegistered(FGameplayTag RoomTag) const
{
	return RoomRuntimes.Contains(RoomTag);
}

ERoomCombatState URoomCombatSubsystem::GetRoomState(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime ? Runtime->State : ERoomCombatState::Dormant;
}

int32 URoomCombatSubsystem::GetCurrentCombatPhaseIndex(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime ? Runtime->CurrentCombatPhaseIndex : INDEX_NONE;
}

int32 URoomCombatSubsystem::GetCurrentWaveIndex(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime ? Runtime->CurrentWaveIndex : INDEX_NONE;
}

int32 URoomCombatSubsystem::GetAliveEnemyCount(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		return 0;
	}

	int32 AliveCount = 0;
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Runtime->AlivePreplacedEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			++AliveCount;
		}
	}
	return AliveCount;
}

void URoomCombatSubsystem::CompleteCurrentPhase(
	FGameplayTag RoomTag,
	bool bCancelRemainingWaves)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	URoomCombatDefinition* Definition = Runtime ? Runtime->Definition.Get() : nullptr;
	if (!Runtime || !Definition
		|| !Definition->CombatPhases.IsValidIndex(Runtime->CurrentCombatPhaseIndex))
	{
		return;
	}

	if (ActiveCombatRoomTag == RoomTag)
	{
		ActiveCombatRoomTag = FGameplayTag();
	}
	if (UWorld* World = GetWorld())
	{
		if (UEnemyRoomSubsystem* EnemyRoomSubsystem =
			World->GetSubsystem<UEnemyRoomSubsystem>())
		{
			// 차수 종료 뒤 해킹을 기다리는 동안 기존 AI 공유 상태가 전투를 계속 붙잡지 않게 한다.
			EnemyRoomSubsystem->NotifyRoomCombatEnded(RoomTag);
		}
	}

	const int32 CompletedPhaseIndex = Runtime->CurrentCombatPhaseIndex;
	const int32 NextPhaseIndex = CompletedPhaseIndex + 1;
	if (!Definition->CombatPhases.IsValidIndex(NextPhaseIndex))
	{
		MarkRoomCleared(RoomTag, *Runtime);
		return;
	}

	Runtime->CurrentCombatPhaseIndex = NextPhaseIndex;
	Runtime->CurrentWaveIndex = 0;
	Runtime->State = Definition->CombatPhases[NextPhaseIndex].StartPolicy
		== ERoomCombatPhaseStartPolicy::HackTrigger
		? ERoomCombatState::WaitingForTrigger
		: ERoomCombatState::Dormant;

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Phase completed. RoomTag=%s Phase=%d CancelledRemainingWaves=%d NextPhase=%d State=%d"),
		*RoomTag.ToString(),
		CompletedPhaseIndex,
		bCancelRemainingWaves ? 1 : 0,
		NextPhaseIndex,
		static_cast<int32>(Runtime->State));
}

void URoomCombatSubsystem::MarkRoomCleared(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	Runtime.State = ERoomCombatState::Cleared;
	if (Runtime.bEncounterIdRegistered)
	{
		UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
			? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->RecordCompletedEncounter(RoomTag.GetTagName());
		}
	}
}

void URoomCombatSubsystem::CompactAliveEnemies(FRoomCombatRuntime& Runtime)
{
	for (auto EnemyIt = Runtime.AlivePreplacedEnemies.CreateIterator(); EnemyIt; ++EnemyIt)
	{
		if (!(*EnemyIt).IsValid())
		{
			RegisteredEnemyRooms.Remove(*EnemyIt);
			EnemyIt.RemoveCurrent();
		}
	}
}

void URoomCombatSubsystem::HandleArenaReleased()
{
	ResetRuntimeCombatState();
}
