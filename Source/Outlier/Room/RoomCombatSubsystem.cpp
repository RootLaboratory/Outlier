#include "Room/RoomCombatSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Subsystems/SubsystemCollection.h"

namespace RoomCombat
{
	int32 SelectWeightedSpawnPoint(
		const TArray<float>& Weights,
		const TArray<int32>& AssignedCounts,
		int32 TieBreakOffset)
	{
		if (Weights.IsEmpty() || Weights.Num() != AssignedCounts.Num())
		{
			return INDEX_NONE;
		}

		int32 SelectedIndex = INDEX_NONE;
		double SelectedScore = TNumericLimits<double>::Max();
		for (int32 Offset = 0; Offset < Weights.Num(); ++Offset)
		{
			const int32 CandidateIndex = (TieBreakOffset + Offset) % Weights.Num();
			if (Weights[CandidateIndex] <= 0.0f || AssignedCounts[CandidateIndex] < 0)
			{
				continue;
			}

			const double CandidateScore = static_cast<double>(AssignedCounts[CandidateIndex])
				/ static_cast<double>(Weights[CandidateIndex]);
			if (CandidateScore + static_cast<double>(KINDA_SMALL_NUMBER) < SelectedScore)
			{
				SelectedIndex = CandidateIndex;
				SelectedScore = CandidateScore;
			}
		}
		return SelectedIndex;
	}
}

namespace
{
	constexpr float SpawnRetryIntervalSeconds = 0.5f;
}

void URoomCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UOutlierArenaSubsystem* ArenaSubsystem =
		Collection.InitializeDependency<UOutlierArenaSubsystem>())
	{
		ArenaSubsystem->OnArenaGameplayReloadStarted.AddUObject(
			this,
			&URoomCombatSubsystem::HandleArenaGameplayReloadStarted);
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
			ArenaSubsystem->OnArenaGameplayReloadStarted.RemoveAll(this);
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
				RegisteredEnemies.Remove(EnemyPtr);
				continue;
			}

			Runtime.TrackedAliveEnemies.Add(EnemyPtr);
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
	RoomVolume->SetCombatStreamingSourceEnabled(false);
	if (UWorld* World = GetWorld())
	{
		if (UEnemyRoomSubsystem* EnemyRoomSubsystem =
			World->GetSubsystem<UEnemyRoomSubsystem>())
		{
			EnemyRoomSubsystem->NotifyRoomCombatEnded(RoomTag);
		}
	}
	TArray<AEnemyBase*> PooledEnemiesToReturn;
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Runtime->TrackedAliveEnemies)
	{
		const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
		if (!EnemyPtr.IsValid() || !Registration)
		{
			continue;
		}

		if (Registration->bPreplaced)
		{
			PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
		}
		else
		{
			PooledEnemiesToReturn.Add(EnemyPtr.Get());
		}
	}

	PendingSpawnRequests.RemoveAll(
		[RoomTag](const FRoomCombatPendingSpawn& Request)
		{
			return Request.RoomTag == RoomTag;
		});
	RoomRuntimes.Remove(RoomTag);

	// Pool 반환은 UnregisterEnemy를 다시 호출하므로 Runtime 제거 뒤 별도 목록으로 처리한다.
	if (UEnemyPoolSubsystem* PoolSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyPoolSubsystem>()
		: nullptr)
	{
		for (AEnemyBase* Enemy : PooledEnemiesToReturn)
		{
			if (IsValid(Enemy))
			{
				PoolSubsystem->ReturnEnemy(
					Enemy,
					Enemy->GetPoolGameplayGeneration(),
					Enemy->GetPoolLeaseSerial());
			}
		}
	}

	if (PendingSpawnRequests.IsEmpty())
	{
		CancelSpawnRetry();
	}
}

bool URoomCombatSubsystem::RegisterSpawnPoint(
	ARoomCombatSpawnPoint* SpawnPoint,
	FGameplayTag RoomTag,
	const FGameplayTagContainer& SpawnPointTags,
	FGameplayTag ActivationGroupTag)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| !IsValid(SpawnPoint) || !SpawnPoint->HasAuthority()
		|| !RoomTag.IsValid())
	{
		return false;
	}

	const TWeakObjectPtr<ARoomCombatSpawnPoint> SpawnPointPtr(SpawnPoint);
	if (const FGameplayTag* ExistingRoomTag = RegisteredSpawnPointRooms.Find(SpawnPointPtr))
	{
		if (*ExistingRoomTag != RoomTag)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomCombat] SpawnPoint registered to multiple Rooms. SpawnPoint=%s Existing=%s New=%s"),
				*GetNameSafe(SpawnPoint),
				*ExistingRoomTag->ToString(),
				*RoomTag.ToString());
			return false;
		}
		// WP 재등록이나 중복 초기화가 같은 Actor를 후보 배열에 두 번 넣지 않게 한다.
		return true;
	}

	FRoomCombatSpawnPointRuntime& Runtime = SpawnPointsByRoom.FindOrAdd(RoomTag).AddDefaulted_GetRef();
	Runtime.SpawnPoint = SpawnPointPtr;
	Runtime.SpawnPointTags = SpawnPointTags;
	Runtime.ActivationGroupTag = ActivationGroupTag;
	RegisteredSpawnPointRooms.Add(SpawnPointPtr, RoomTag);
	return true;
}

void URoomCombatSubsystem::UnregisterSpawnPoint(ARoomCombatSpawnPoint* SpawnPoint)
{
	if (!SpawnPoint)
	{
		return;
	}

	const TWeakObjectPtr<ARoomCombatSpawnPoint> SpawnPointPtr(SpawnPoint);
	const FGameplayTag* RoomTag = RegisteredSpawnPointRooms.Find(SpawnPointPtr);
	if (!RoomTag)
	{
		return;
	}

	if (TArray<FRoomCombatSpawnPointRuntime>* RoomSpawnPoints =
		SpawnPointsByRoom.Find(*RoomTag))
	{
		RoomSpawnPoints->RemoveAll(
			[SpawnPoint](const FRoomCombatSpawnPointRuntime& Runtime)
			{
				return Runtime.SpawnPoint.Get() == SpawnPoint;
			});
		if (RoomSpawnPoints->IsEmpty())
		{
			SpawnPointsByRoom.Remove(*RoomTag);
		}
	}

	RegisteredSpawnPointRooms.Remove(SpawnPointPtr);
}

void URoomCombatSubsystem::SetActivationGroupActive(
	FGameplayTag RoomTag,
	FGameplayTag ActivationGroupTag,
	bool bActive)
{
	if (!RoomTag.IsValid() || !ActivationGroupTag.IsValid())
	{
		return;
	}

	CompactSpawnPoints(RoomTag);
	TArray<FRoomCombatSpawnPointRuntime>* RoomSpawnPoints = SpawnPointsByRoom.Find(RoomTag);
	if (!RoomSpawnPoints)
	{
		return;
	}

	for (const FRoomCombatSpawnPointRuntime& Runtime : *RoomSpawnPoints)
	{
		// 그룹 태그는 후보 선택 조건이 아니라 해킹 성공 시 같은 그룹의 런타임 활성도를 묶는 키다.
		if (Runtime.ActivationGroupTag == ActivationGroupTag)
		{
			if (ARoomCombatSpawnPoint* SpawnPoint = Runtime.SpawnPoint.Get())
			{
				SpawnPoint->SetRuntimeActive(bActive);
			}
		}
	}
}

void URoomCombatSubsystem::GetEligibleSpawnPoints(
	FGameplayTag RoomTag,
	const FGameplayTagQuery& SpawnPointQuery,
	TArray<ARoomCombatSpawnPoint*>& OutSpawnPoints)
{
	OutSpawnPoints.Reset();
	// 주변 Room의 셀이 함께 로드되어 있어도 동시에 진행 중인 하나의 Room만 소환 후보를 제공한다.
	if (!RoomTag.IsValid() || ActiveCombatRoomTag != RoomTag)
	{
		return;
	}

	CompactSpawnPoints(RoomTag);
	const TArray<FRoomCombatSpawnPointRuntime>* RoomSpawnPoints =
		SpawnPointsByRoom.Find(RoomTag);
	if (!RoomSpawnPoints)
	{
		return;
	}

	for (const FRoomCombatSpawnPointRuntime& Runtime : *RoomSpawnPoints)
	{
		ARoomCombatSpawnPoint* SpawnPoint = Runtime.SpawnPoint.Get();
		if (!SpawnPoint || !SpawnPoint->IsRuntimeActive())
		{
			continue;
		}
		if (!SpawnPointQuery.IsEmpty()
			&& !SpawnPointQuery.Matches(Runtime.SpawnPointTags))
		{
			continue;
		}

		OutSpawnPoints.Add(SpawnPoint);
	}
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
	FRoomCombatEnemyRegistration& Registration = RegisteredEnemies.FindOrAdd(EnemyPtr);
	Registration.RoomTag = RoomTag;
	Registration.CombatPhaseIndex = 0;
	Registration.WaveIndex = 0;
	Registration.GameplayGeneration = 0;
	Registration.bPreplaced = true;

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
		return;
	}

	if (Runtime->State == ERoomCombatState::Cleared)
	{
		RegisteredEnemies.Remove(EnemyPtr);
		Enemy->Destroy();
		return;
	}

	Runtime->TrackedAliveEnemies.Add(EnemyPtr);
	Runtime->bHadPreplacedEnemy = true;
}

void URoomCombatSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
	if (!Registration)
	{
		return;
	}
	const FGameplayTag RegisteredRoomTag = Registration->RoomTag;

	if (FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RegisteredRoomTag))
	{
		Runtime->TrackedAliveEnemies.Remove(EnemyPtr);
	}
	if (TSet<TWeakObjectPtr<AEnemyBase>>* PendingEnemies =
		PendingPreplacedEnemies.Find(RegisteredRoomTag))
	{
		PendingEnemies->Remove(EnemyPtr);
		if (PendingEnemies->IsEmpty())
		{
			PendingPreplacedEnemies.Remove(RegisteredRoomTag);
		}
	}
	RegisteredEnemies.Remove(EnemyPtr);
}

void URoomCombatSubsystem::NotifyEnemyDefeated(AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->HasAuthority())
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
	if (!Registration)
	{
		return;
	}

	const FGameplayTag RoomTag = Registration->RoomTag;
	const bool bWasPreplaced = Registration->bPreplaced;
	RegisteredEnemies.Remove(EnemyPtr);
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

	Runtime->TrackedAliveEnemies.Remove(EnemyPtr);
	CompactAliveEnemies(*Runtime);
	if (!bWasPreplaced || !Runtime->bHadPreplacedEnemy
		|| !Runtime->TrackedAliveEnemies.IsEmpty())
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
	if (Runtime->TrackedAliveEnemies.IsEmpty())
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
	Runtime->bCurrentWaveSpawnStarted = true;
	ActiveCombatRoomTag = RoomTag;
	SetRoomStreamingSourceEnabled(RoomTag, true);
	return true;
}

bool URoomCombatSubsystem::StartWaveSpawning(
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex)
{
	UWorld* World = GetWorld();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	URoomCombatDefinition* Definition = Runtime ? Runtime->Definition.Get() : nullptr;
	if (!World || World->GetNetMode() == NM_Client || !Runtime || !Definition
		|| Runtime->State != ERoomCombatState::Combat
		|| ActiveCombatRoomTag != RoomTag
		|| Runtime->CurrentCombatPhaseIndex != CombatPhaseIndex
		|| !Definition->CombatPhases.IsValidIndex(CombatPhaseIndex))
	{
		return false;
	}

	const FRoomCombatPhaseDefinition& Phase = Definition->CombatPhases[CombatPhaseIndex];
	if (!Phase.Waves.IsValidIndex(WaveIndex)
		|| Phase.Waves[WaveIndex].SpawnMode != ERoomCombatWaveSpawnMode::SpawnFromObjects
		|| (WaveIndex < Runtime->CurrentWaveIndex)
		|| (WaveIndex == Runtime->CurrentWaveIndex && Runtime->bCurrentWaveSpawnStarted)
		|| WaveIndex > Runtime->CurrentWaveIndex + 1
		|| !PendingSpawnRequests.IsEmpty())
	{
		return false;
	}

	const FRoomCombatWaveDefinition& Wave = Phase.Waves[WaveIndex];
	TArray<TSubclassOf<AEnemyBase>> EnemyRoster;
	for (const FRoomCombatEnemyEntry& Entry : Wave.Enemies)
	{
		UClass* LoadedClass = Entry.EnemyClass.LoadSynchronous();
		if (!LoadedClass || !LoadedClass->IsChildOf(AEnemyBase::StaticClass()) || Entry.Count < 1)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomCombat] Wave spawn rejected by invalid roster. Room=%s Phase=%d Wave=%d Class=%s Count=%d"),
				*RoomTag.ToString(),
				CombatPhaseIndex,
				WaveIndex,
				*GetNameSafe(LoadedClass),
				Entry.Count);
			return false;
		}

		EnemyRoster.Reserve(EnemyRoster.Num() + Entry.Count);
		for (int32 EnemyIndex = 0; EnemyIndex < Entry.Count; ++EnemyIndex)
		{
			EnemyRoster.Add(LoadedClass);
		}
	}

	const UOutlierArenaSubsystem* ArenaSubsystem =
		World->GetSubsystem<UOutlierArenaSubsystem>();
	const int32 GameplayGeneration = ArenaSubsystem
		? static_cast<int32>(ArenaSubsystem->GetGameplayGeneration())
		: 0;
	Runtime->CurrentWaveIndex = WaveIndex;
	Runtime->GameplayGeneration = GameplayGeneration;
	Runtime->bCurrentWaveSpawnStarted = true;
	Runtime->SpawnAssignments.Reset();

	TArray<ARoomCombatSpawnPoint*> EligibleSpawnPoints;
	GetEligibleSpawnPoints(RoomTag, Wave.SpawnPointQuery, EligibleSpawnPoints);
	TArray<float> Weights;
	TArray<int32> AssignedCounts;
	for (ARoomCombatSpawnPoint* SpawnPoint : EligibleSpawnPoints)
	{
		Weights.Add(SpawnPoint->GetSpawnWeight());
		AssignedCounts.Add(0);
	}

	for (int32 RosterIndex = 0; RosterIndex < EnemyRoster.Num(); ++RosterIndex)
	{
		FRoomCombatPendingSpawn& Request = PendingSpawnRequests.AddDefaulted_GetRef();
		Request.EnemyClass = EnemyRoster[RosterIndex];
		Request.RoomTag = RoomTag;
		Request.SpawnPointQuery = Wave.SpawnPointQuery;
		Request.CombatPhaseIndex = CombatPhaseIndex;
		Request.WaveIndex = WaveIndex;
		Request.GameplayGeneration = GameplayGeneration;

		const int32 SpawnPointIndex = RoomCombat::SelectWeightedSpawnPoint(
			Weights,
			AssignedCounts,
			RosterIndex);
		if (EligibleSpawnPoints.IsValidIndex(SpawnPointIndex))
		{
			Request.AssignedSpawnPoint = EligibleSpawnPoints[SpawnPointIndex];
			++AssignedCounts[SpawnPointIndex];
			Runtime->SpawnAssignments.FindOrAdd(EligibleSpawnPoints[SpawnPointIndex])++;
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave roster queued. Room=%s Phase=%d Wave=%d Generation=%d Pending=%d SpawnPoints=%d"),
		*RoomTag.ToString(),
		CombatPhaseIndex,
		WaveIndex,
		GameplayGeneration,
		EnemyRoster.Num(),
		EligibleSpawnPoints.Num());

	TrySpawnPendingRequests(RoomTag);
	return true;
}

bool URoomCombatSubsystem::RegisterSpawnedEnemy(
	AEnemyBase* Enemy,
	const FRoomCombatPendingSpawn& SpawnRequest)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(SpawnRequest.RoomTag);
	if (!IsValid(Enemy) || !Runtime
		|| Runtime->GameplayGeneration != SpawnRequest.GameplayGeneration
		|| Runtime->CurrentCombatPhaseIndex != SpawnRequest.CombatPhaseIndex
		|| Runtime->CurrentWaveIndex != SpawnRequest.WaveIndex)
	{
		return false;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	if (RegisteredEnemies.Contains(EnemyPtr))
	{
		return false;
	}

	FRoomCombatEnemyRegistration& Registration = RegisteredEnemies.Add(EnemyPtr);
	Registration.RoomTag = SpawnRequest.RoomTag;
	Registration.CombatPhaseIndex = SpawnRequest.CombatPhaseIndex;
	Registration.WaveIndex = SpawnRequest.WaveIndex;
	Registration.GameplayGeneration = SpawnRequest.GameplayGeneration;
	Registration.bPreplaced = false;
	// 소환 연출 중에도 이 전투 차수의 생존 적이다. AI 활성 완료 여부와 생존 집계를 분리한다.
	Runtime->TrackedAliveEnemies.Add(EnemyPtr);
	return true;
}

void URoomCombatSubsystem::TrySpawnPendingRequests(FGameplayTag RoomTag)
{
	UWorld* World = GetWorld();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	UEnemyPoolSubsystem* PoolSubsystem = World
		? World->GetSubsystem<UEnemyPoolSubsystem>()
		: nullptr;
	if (!World || !Runtime || !PoolSubsystem || Runtime->State != ERoomCombatState::Combat)
	{
		ScheduleSpawnRetry();
		return;
	}

	for (int32 RequestIndex = PendingSpawnRequests.Num() - 1; RequestIndex >= 0; --RequestIndex)
	{
		FRoomCombatPendingSpawn& Request = PendingSpawnRequests[RequestIndex];
		if (Request.RoomTag != RoomTag)
		{
			continue;
		}

		if (Request.GameplayGeneration != Runtime->GameplayGeneration
			|| Request.CombatPhaseIndex != Runtime->CurrentCombatPhaseIndex
			|| Request.WaveIndex != Runtime->CurrentWaveIndex)
		{
			PendingSpawnRequests.RemoveAtSwap(RequestIndex, 1, EAllowShrinking::No);
			continue;
		}

		TArray<ARoomCombatSpawnPoint*> EligibleSpawnPoints;
		GetEligibleSpawnPoints(RoomTag, Request.SpawnPointQuery, EligibleSpawnPoints);
		ARoomCombatSpawnPoint* SpawnPoint = ResolveSpawnPoint(
			*Runtime,
			Request,
			EligibleSpawnPoints);
		if (!SpawnPoint)
		{
			continue;
		}

		FTransform SpawnTransform;
		const int32 SearchSeed = HashCombineFast(
			GetTypeHash(Request.GameplayGeneration),
			HashCombineFast(GetTypeHash(Request.WaveIndex), ++Request.SearchSerial));
		if (!SpawnPoint->FindSpawnTransform(Request.EnemyClass, SearchSeed, SpawnTransform))
		{
			continue;
		}

		FEnemyPoolLeaseContext LeaseContext;
		LeaseContext.RoomTag = Request.RoomTag;
		LeaseContext.CombatPhaseIndex = Request.CombatPhaseIndex;
		LeaseContext.WaveIndex = Request.WaveIndex;
		LeaseContext.GameplayGeneration = Request.GameplayGeneration;
		AEnemyBase* Enemy = PoolSubsystem->LeaseEnemy(
			Request.EnemyClass,
			SpawnTransform,
			LeaseContext);
		if (!Enemy)
		{
			continue;
		}

		if (!RegisterSpawnedEnemy(Enemy, Request))
		{
			PoolSubsystem->ReturnEnemy(
				Enemy,
				Enemy->GetPoolGameplayGeneration(),
				Enemy->GetPoolLeaseSerial());
			continue;
		}

		PendingSpawnRequests.RemoveAtSwap(RequestIndex, 1, EAllowShrinking::No);
	}

	if (PendingSpawnRequests.IsEmpty())
	{
		Runtime->SpawnRetryAttempts = 0;
		CancelSpawnRetry();
		return;
	}

	++Runtime->SpawnRetryAttempts;
	const double CurrentTimeSeconds = World->GetTimeSeconds();
	if (CurrentTimeSeconds - Runtime->LastSpawnRetryLogSeconds >= 5.0)
	{
		Runtime->LastSpawnRetryLogSeconds = CurrentTimeSeconds;
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomCombat] Wave spawn remains pending. Room=%s Phase=%d Wave=%d Pending=%d Attempts=%d"),
			*RoomTag.ToString(),
			Runtime->CurrentCombatPhaseIndex,
			Runtime->CurrentWaveIndex,
			GetPendingSpawnCount(RoomTag),
			Runtime->SpawnRetryAttempts);
	}
	ScheduleSpawnRetry();
}

ARoomCombatSpawnPoint* URoomCombatSubsystem::ResolveSpawnPoint(
	FRoomCombatRuntime& Runtime,
	FRoomCombatPendingSpawn& SpawnRequest,
	const TArray<ARoomCombatSpawnPoint*>& EligibleSpawnPoints)
{
	ARoomCombatSpawnPoint* AssignedSpawnPoint = SpawnRequest.AssignedSpawnPoint.Get();
	if (AssignedSpawnPoint && EligibleSpawnPoints.Contains(AssignedSpawnPoint))
	{
		return AssignedSpawnPoint;
	}

	if (AssignedSpawnPoint)
	{
		if (int32* Count = Runtime.SpawnAssignments.Find(AssignedSpawnPoint))
		{
			*Count = FMath::Max(0, *Count - 1);
		}
	}

	TArray<float> Weights;
	TArray<int32> AssignedCounts;
	for (ARoomCombatSpawnPoint* SpawnPoint : EligibleSpawnPoints)
	{
		Weights.Add(SpawnPoint->GetSpawnWeight());
		AssignedCounts.Add(Runtime.SpawnAssignments.FindRef(SpawnPoint));
	}
	const int32 SelectedIndex = RoomCombat::SelectWeightedSpawnPoint(
		Weights,
		AssignedCounts,
		SpawnRequest.SearchSerial);
	if (!EligibleSpawnPoints.IsValidIndex(SelectedIndex))
	{
		SpawnRequest.AssignedSpawnPoint.Reset();
		return nullptr;
	}

	AssignedSpawnPoint = EligibleSpawnPoints[SelectedIndex];
	SpawnRequest.AssignedSpawnPoint = AssignedSpawnPoint;
	Runtime.SpawnAssignments.FindOrAdd(AssignedSpawnPoint)++;
	return AssignedSpawnPoint;
}

void URoomCombatSubsystem::RetryPendingSpawns()
{
	if (!ActiveCombatRoomTag.IsValid())
	{
		CancelSpawnRetry();
		return;
	}

	const UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(ActiveCombatRoomTag);
	if (!Runtime || (ArenaSubsystem
		&& Runtime->GameplayGeneration != static_cast<int32>(ArenaSubsystem->GetGameplayGeneration())))
	{
		PendingSpawnRequests.Reset();
		CancelSpawnRetry();
		return;
	}

	TrySpawnPendingRequests(ActiveCombatRoomTag);
}

void URoomCombatSubsystem::ScheduleSpawnRetry()
{
	UWorld* World = GetWorld();
	if (!World || PendingSpawnRequests.IsEmpty()
		|| World->GetTimerManager().IsTimerActive(SpawnRetryTimer))
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		SpawnRetryTimer,
		this,
		&URoomCombatSubsystem::RetryPendingSpawns,
		SpawnRetryIntervalSeconds,
		true);
}

void URoomCombatSubsystem::CancelSpawnRetry()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnRetryTimer);
	}
}

void URoomCombatSubsystem::ResetRuntimeCombatState()
{
	CancelSpawnRetry();
	for (const TPair<FGameplayTag, FRoomCombatRuntime>& Entry : RoomRuntimes)
	{
		if (ARoomVolume* RoomVolume = Entry.Value.RoomVolume.Get())
		{
			RoomVolume->SetCombatStreamingSourceEnabled(false);
		}
	}

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
	RegisteredEnemies.Reset();
	PendingSpawnRequests.Reset();
	SpawnPointsByRoom.Reset();
	RegisteredSpawnPointRooms.Reset();
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
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Runtime->TrackedAliveEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			++AliveCount;
		}
	}
	return AliveCount;
}

int32 URoomCombatSubsystem::GetPendingSpawnCount(FGameplayTag RoomTag) const
{
	int32 PendingCount = 0;
	for (const FRoomCombatPendingSpawn& Request : PendingSpawnRequests)
	{
		if (Request.RoomTag == RoomTag)
		{
			++PendingCount;
		}
	}
	return PendingCount;
}

int32 URoomCombatSubsystem::GetAssignedSpawnCount(
	FGameplayTag RoomTag,
	const ARoomCombatSpawnPoint* SpawnPoint) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && SpawnPoint
		? Runtime->SpawnAssignments.FindRef(SpawnPoint)
		: 0;
}

int32 URoomCombatSubsystem::GetRegisteredSpawnPointCount(FGameplayTag RoomTag)
{
	CompactSpawnPoints(RoomTag);
	const TArray<FRoomCombatSpawnPointRuntime>* RoomSpawnPoints =
		SpawnPointsByRoom.Find(RoomTag);
	return RoomSpawnPoints ? RoomSpawnPoints->Num() : 0;
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
	SetRoomStreamingSourceEnabled(RoomTag, false);
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
	Runtime->bCurrentWaveSpawnStarted = false;
	Runtime->SpawnAssignments.Reset();
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
	for (auto EnemyIt = Runtime.TrackedAliveEnemies.CreateIterator(); EnemyIt; ++EnemyIt)
	{
		if (!(*EnemyIt).IsValid())
		{
			RegisteredEnemies.Remove(*EnemyIt);
			EnemyIt.RemoveCurrent();
		}
	}
}

void URoomCombatSubsystem::CompactSpawnPoints(FGameplayTag RoomTag)
{
	TArray<FRoomCombatSpawnPointRuntime>* RoomSpawnPoints =
		SpawnPointsByRoom.Find(RoomTag);
	if (!RoomSpawnPoints)
	{
		return;
	}

	RoomSpawnPoints->RemoveAll(
		[this](const FRoomCombatSpawnPointRuntime& Runtime)
		{
			if (Runtime.SpawnPoint.IsValid())
			{
				return false;
			}

			// EndPlay 해제를 받지 못한 경우에도 만료된 WP Actor의 역방향 인덱스를 함께 정리한다.
			RegisteredSpawnPointRooms.Remove(Runtime.SpawnPoint);
			return true;
		});

	if (RoomSpawnPoints->IsEmpty())
	{
		SpawnPointsByRoom.Remove(RoomTag);
	}
}

void URoomCombatSubsystem::SetRoomStreamingSourceEnabled(
	FGameplayTag RoomTag,
	bool bEnabled)
{
	if (FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag))
	{
		if (ARoomVolume* RoomVolume = Runtime->RoomVolume.Get())
		{
			RoomVolume->SetCombatStreamingSourceEnabled(bEnabled);
		}
	}
}

void URoomCombatSubsystem::HandleArenaGameplayReloadStarted(uint32 GameplayGeneration)
{
	(void)GameplayGeneration;
	ResetRuntimeCombatState();
}

void URoomCombatSubsystem::HandleArenaReleased()
{
	ResetRuntimeCombatState();
}
