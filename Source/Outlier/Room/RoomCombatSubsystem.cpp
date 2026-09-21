#include "Room/RoomCombatSubsystem.h"

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
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

	bool MatchesCurrentWave(const FRoomCombatRuntime& Runtime, const FRoomCombatPendingSpawn& Request)
	{
		return Runtime.GameplayGeneration == Request.GameplayGeneration
			&& Runtime.CurrentCombatPhaseIndex == Request.CombatPhaseIndex
			&& Runtime.CurrentWaveIndex == Request.WaveIndex;
	}

	bool IsPendingSpawnForRoom(const FRoomCombatPendingSpawn& Request, FGameplayTag RoomTag)
	{
		return Request.RoomTag == RoomTag;
	}

	bool BuildWaveEnemyRoster(const FRoomCombatWaveDefinition& Wave,
		FGameplayTag RoomTag, int32 PhaseIndex, int32 WaveIndex,
		TArray<TSubclassOf<AEnemyBase>>& OutRoster)
	{
		OutRoster.Reset();
		// 전체 명단을 먼저 검증한다. 중간 항목이 잘못되어도 일부 요청만 등록되는 일이 없게 한다.
		for (const FRoomCombatEnemyEntry& Entry : Wave.Enemies)
		{
			UClass* LoadedClass = Entry.EnemyClass.LoadSynchronous();
			if (!LoadedClass || !LoadedClass->IsChildOf(AEnemyBase::StaticClass()) || Entry.Count < 1)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomCombat] Wave spawn rejected by invalid roster. Room=%s Phase=%d Wave=%d Class=%s Count=%d"),
					*RoomTag.ToString(), PhaseIndex, WaveIndex, *GetNameSafe(LoadedClass), Entry.Count);
				return false;
			}
			OutRoster.Reserve(OutRoster.Num() + Entry.Count);
			for (int32 EnemyIndex = 0; EnemyIndex < Entry.Count; ++EnemyIndex)
			{
				OutRoster.Add(LoadedClass);
			}
		}
		return !OutRoster.IsEmpty() || Wave.HasWaveTurrets();
	}
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
	CombatDefinition = nullptr;
	Super::Deinitialize();
}

void URoomCombatSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	CombatDefinition = Settings
		? Settings->RoomCombatDefinition.LoadSynchronous()
		: nullptr;
	if (!CombatDefinition)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomCombat] Integrated RoomCombatDefinition is not configured."));
	}
}

const FRoomCombatRoomDefinition* URoomCombatSubsystem::FindRoomDefinition(FGameplayTag RoomTag) const
{
	return CombatDefinition
		? CombatDefinition->FindRoomDefinition(RoomTag)
		: nullptr;
}

bool URoomCombatSubsystem::CanRunServerGameplay() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && !bResettingRuntime;
}

bool URoomCombatSubsystem::HasPendingSpawns(FGameplayTag RoomTag) const
{
	return PendingSpawnRequests.ContainsByPredicate(
		[RoomTag](const FRoomCombatPendingSpawn& Request)
		{
			return IsPendingSpawnForRoom(Request, RoomTag);
		});
}

bool URoomCombatSubsystem::HasPendingWaveWork(
	FGameplayTag RoomTag,
	const FRoomCombatRuntime& Runtime) const
{
	return HasPendingSpawns(RoomTag) || Runtime.PendingActivationCount > 0;
}

bool URoomCombatSubsystem::IsActiveCombatRuntime(
	FGameplayTag RoomTag,
	const FRoomCombatRuntime& Runtime) const
{
	return Runtime.State == ERoomCombatState::Combat && ActiveCombatRoomTag == RoomTag;
}

bool URoomCombatSubsystem::CreateTriggerContext(
	AActor* Requester, FGameplayTag RoomTag, FGameplayTag ActivationGroupTag,
	FRoomCombatTriggerContext& OutContext) const
{
	OutContext = FRoomCombatTriggerContext();
	// 해킹 시작 당시의 수명만 기록한다. Room을 예약하지 않으므로 성공 시점에 시작 조건을 다시 검사한다.
	UWorld* World = GetWorld();
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* Phase = Definition && Runtime
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
		: nullptr;
	if (!CanRunServerGameplay()
		|| !IsValid(Requester) || Requester->IsActorBeingDestroyed()
		|| !Requester->HasAuthority() || Requester->GetWorld() != World
		|| !ActivationGroupTag.IsValid() || !Runtime || !Runtime->RoomVolume.IsValid()
		|| Runtime->State != ERoomCombatState::WaitingForTrigger
		|| ActiveCombatRoomTag.IsValid()
		|| !Phase || Phase->StartPolicy != ERoomCombatPhaseStartPolicy::HackTrigger)
	{
		return false;
	}
	const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	OutContext.RoomTag = RoomTag;
	OutContext.ActivationGroupTag = ActivationGroupTag;
	OutContext.CombatPhaseIndex = Runtime->CurrentCombatPhaseIndex;
	OutContext.GameplayGeneration = Arena ? static_cast<int32>(Arena->GetGameplayGeneration()) : 0;
	OutContext.RoomRegistrationId = Runtime->RegistrationId;
	OutContext.Requester = Requester;
	return true;
}

bool URoomCombatSubsystem::StartTriggeredSequence(
	AActor* Requester, const FRoomCombatTriggerContext& Context)
{
	FRoomCombatTriggerContext CurrentContext;
	// 해킹 중 리로드되거나 같은 태그의 Room이 재등록되면 이전 성공 콜백으로 전투를 시작할 수 없다.
	if (!CreateTriggerContext(Requester, Context.RoomTag, Context.ActivationGroupTag, CurrentContext)
		|| Context.Requester.Get() != Requester
		|| Context.RoomRegistrationId != CurrentContext.RoomRegistrationId
		|| Context.GameplayGeneration != CurrentContext.GameplayGeneration
		|| Context.CombatPhaseIndex != CurrentContext.CombatPhaseIndex)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[RoomCombat] Trigger rejected. Room=%s Phase=%d Generation=%d"),
			*Context.RoomTag.ToString(), Context.CombatPhaseIndex, Context.GameplayGeneration);
		return false;
	}

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(Context.RoomTag);
	if (!Definition)
	{
		return false;
	}
	if (!Definition->CanStartTriggeredSequence(Context.CombatPhaseIndex)
		|| !PendingSpawnRequests.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomCombat] Invalid triggered sequence. Room=%s Phase=%d"),
			*Context.RoomTag.ToString(), Context.CombatPhaseIndex);
		return false;
	}

	Runtime->State = ERoomCombatState::Combat;
	Runtime->CurrentWaveIndex = 0;
	Runtime->bTriggeredSequenceActive = true;
	Runtime->bDeferSpawnExecution = true;
	Runtime->ActiveActivationGroupTag = Context.ActivationGroupTag;
	ActiveCombatRoomTag = Context.RoomTag;
	SetActivationGroupActive(Context.RoomTag, Context.ActivationGroupTag, true);
	SetRoomStreamingSourceEnabled(Context.RoomTag, true);
	if (!StartWaveSpawning(Context.RoomTag, Context.CombatPhaseIndex, 0))
	{
		Runtime->State = ERoomCombatState::WaitingForTrigger;
		Runtime->bTriggeredSequenceActive = false;
		Runtime->bDeferSpawnExecution = false;
		Runtime->ActiveActivationGroupTag = FGameplayTag();
		SetActivationGroupActive(Context.RoomTag, Context.ActivationGroupTag, false);
		SetRoomStreamingSourceEnabled(Context.RoomTag, false);
		ActiveCombatRoomTag = FGameplayTag();
		return false;
	}

	// BP가 이 이벤트에서 벽을 막은 뒤 소환을 실행한다. BP에서 Reset했다면 재개하지 않는다.
	// 호출자가 저장한 Context도 이벤트 중 바뀔 수 있으므로 검증된 복사본으로 재개한다.
	BroadcastCombatEvent(CurrentContext.RoomTag, ERoomCombatEvent::SequenceStarted,
		CurrentContext.CombatPhaseIndex, CurrentContext.GameplayGeneration);
	ResumeDeferredSpawning(CurrentContext.RoomTag, CurrentContext.RoomRegistrationId);
	return true;
}

bool URoomCombatSubsystem::IsExitBlocked(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && Runtime->bTriggeredSequenceActive;
}

void URoomCombatSubsystem::BroadcastCombatEvent(
	FGameplayTag RoomTag, ERoomCombatEvent Event, int32 PhaseIndex, int32 Generation)
{
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Event=%d Room=%s Phase=%d Generation=%d"),
		static_cast<int32>(Event), *RoomTag.ToString(), PhaseIndex, Generation);
	OnCombatEvent.Broadcast(RoomTag, Event, PhaseIndex, Generation);
#if WITH_DEV_AUTOMATION_TESTS
	if (CombatEventObserverForTesting)
	{
		CombatEventObserverForTesting(RoomTag, Event, PhaseIndex);
	}
#endif
}

void URoomCombatSubsystem::ResumeDeferredSpawning(FGameplayTag RoomTag, const FGuid& RegistrationId)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (Runtime && Runtime->RegistrationId == RegistrationId
		&& Runtime->State == ERoomCombatState::Combat && Runtime->bDeferSpawnExecution)
	{
		Runtime->bDeferSpawnExecution = false;
		TryStartPendingWaveTurretActivations(RoomTag, *Runtime);
		TrySpawnPendingRequests(RoomTag);
	}
}

bool URoomCombatSubsystem::RegisterRoom(
	ARoomVolume* RoomVolume,
	FGameplayTag RoomTag)
{
	UWorld* World = GetWorld();
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	if (!CanRunServerGameplay()
		|| !IsValid(RoomVolume) || !RoomTag.IsValid() || !Definition)
	{
		if (World && World->GetNetMode() != NM_Client && IsValid(RoomVolume) && RoomTag.IsValid()
			&& CombatDefinition && !Definition)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[RoomCombat] Room has no integrated combat entry. Room=%s Actor=%s"),
				*RoomTag.ToString(), *GetNameSafe(RoomVolume));
		}
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
	Runtime.RegistrationId = FGuid::NewGuid();
	if (const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>())
	{
		Runtime.GameplayGeneration = static_cast<int32>(Arena->GetGameplayGeneration());
	}

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
	else if (const FRoomCombatPhaseDefinition* FirstPhase = Definition->FindPhase(0);
		FirstPhase && FirstPhase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger)
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
	const bool bWasSequenceActive = Runtime->bTriggeredSequenceActive;
	const int32 CancelledPhase = Runtime->CurrentCombatPhaseIndex;
	const int32 CancelledGeneration = Runtime->GameplayGeneration;
	SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, false);

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

		if (!Registration->bPoolManaged)
		{
			if (Registration->bPreplaced)
			{
				PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
			}
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
	if (bWasSequenceActive)
	{
		BroadcastCombatEvent(RoomTag, ERoomCombatEvent::Cancelled, CancelledPhase, CancelledGeneration);
	}
}

bool URoomCombatSubsystem::RegisterSpawnPoint(
	ARoomCombatSpawnPoint* SpawnPoint,
	FGameplayTag RoomTag,
	const FGameplayTagContainer& SpawnPointTags,
	FGameplayTag ActivationGroupTag)
{
	if (!CanRunServerGameplay()
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
	if (const FRoomCombatRuntime* Room = RoomRuntimes.Find(RoomTag))
	{
		// 그룹을 켠 뒤 WP에서 도착한 지점도 같은 연속 전투에 참여한다.
		if (ActivationGroupTag.IsValid() && Room->ActiveActivationGroupTag == ActivationGroupTag)
		{
			SpawnPoint->SetRuntimeActive(Room->bTriggeredSequenceActive);
		}
	}
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

bool URoomCombatSubsystem::RegisterWaveTurret(
	AAutoTurret* Turret,
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex)
{
	UWorld* World = GetWorld();
	if (!CanRunServerGameplay())
	{
		return false;
	}
	if (!IsValid(Turret) || !Turret->HasAuthority() || Turret->GetWorld() != World)
	{
		return false;
	}
	if (!RoomTag.IsValid() || CombatPhaseIndex < 0 || WaveIndex < 0)
	{
		return false;
	}

	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatWaveDefinition* Wave = Definition
		? Definition->FindWave(CombatPhaseIndex, WaveIndex)
		: nullptr;
	if (!Wave)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret has an invalid Room/Wave. Turret=%s Room=%s Phase=%d Wave=%d"),
			*GetNameSafe(Turret), *RoomTag.ToString(), CombatPhaseIndex, WaveIndex);
		return false;
	}

	const int32 ExpectedTurretCount = Wave->ExpectedWaveTurretCount;
	if (!Wave->IsSpawnFromObjects() || !Wave->HasWaveTurrets())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret targets a Wave that expects no turrets. Turret=%s Room=%s Phase=%d Wave=%d"),
			*GetNameSafe(Turret), *RoomTag.ToString(), CombatPhaseIndex, WaveIndex);
		return false;
	}

	const TWeakObjectPtr<AAutoTurret> TurretPtr(Turret);
	// WP 재등록은 같은 Actor의 BeginPlay가 다시 들어올 수 있으므로 완전히 같은 설정은 멱등 성공으로 본다.
	if (const FGameplayTag* ExistingRoomTag = RegisteredWaveTurretRooms.Find(TurretPtr))
	{
		const TArray<FRoomCombatWaveTurretRuntime>* ExistingEntries =
			WaveTurretsByRoom.Find(*ExistingRoomTag);
		const FRoomCombatWaveTurretRuntime* Existing = ExistingEntries
			? ExistingEntries->FindByPredicate(
				[Turret](const FRoomCombatWaveTurretRuntime& Entry)
				{
					return Entry.Turret.Get() == Turret;
				})
			: nullptr;
		if (Existing && *ExistingRoomTag == RoomTag
			&& Existing->CombatPhaseIndex == CombatPhaseIndex
			&& Existing->WaveIndex == WaveIndex)
		{
			return true;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret was registered with different settings. Turret=%s ExistingRoom=%s NewRoom=%s"),
			*GetNameSafe(Turret), *ExistingRoomTag->ToString(), *RoomTag.ToString());
		return false;
	}

	CompactWaveTurrets(RoomTag);
	if (GetRegisteredWaveTurretCount(RoomTag, CombatPhaseIndex, WaveIndex)
		>= ExpectedTurretCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret count exceeds Wave definition. Turret=%s Room=%s Phase=%d Wave=%d Expected=%d"),
			*GetNameSafe(Turret), *RoomTag.ToString(), CombatPhaseIndex, WaveIndex, ExpectedTurretCount);
		return false;
	}

	FRoomCombatWaveTurretRuntime& Runtime =
		WaveTurretsByRoom.FindOrAdd(RoomTag).AddDefaulted_GetRef();
	Runtime.Turret = TurretPtr;
	Runtime.CombatPhaseIndex = CombatPhaseIndex;
	Runtime.WaveIndex = WaveIndex;
	RegisteredWaveTurretRooms.Add(TurretPtr, RoomTag);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret registered. Turret=%s Room=%s Phase=%d Wave=%d Registered=%d Expected=%d"),
		*GetNameSafe(Turret),
		*RoomTag.ToString(),
		CombatPhaseIndex,
		WaveIndex,
		GetRegisteredWaveTurretCount(RoomTag, CombatPhaseIndex, WaveIndex),
		ExpectedTurretCount);

	// Streaming Source가 켜지는 순간보다 Actor 등록이 늦을 수 있다. 현재 Wave가 이 터렛을
	// 기다리는 중이면 등록 이벤트 자체가 재시도 신호가 되어 별도 Tick 없이 전개를 시작한다.
	if (FRoomCombatRuntime* RoomRuntime = RoomRuntimes.Find(RoomTag);
		RoomRuntime && !RoomRuntime->bDeferSpawnExecution)
	{
		TryStartPendingWaveTurretActivations(RoomTag, *RoomRuntime);
	}
	return true;
}

void URoomCombatSubsystem::UnregisterWaveTurret(AAutoTurret* Turret)
{
	if (!Turret)
	{
		return;
	}

	const TWeakObjectPtr<AAutoTurret> TurretPtr(Turret);
	const FGameplayTag* RoomTag = RegisteredWaveTurretRooms.Find(TurretPtr);
	if (!RoomTag)
	{
		return;
	}
	const FGameplayTag RegisteredRoomTag = *RoomTag;

	if (TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RegisteredRoomTag))
	{
		Entries->RemoveAll(
			[Turret](const FRoomCombatWaveTurretRuntime& Entry)
			{
				return Entry.Turret.Get() == Turret;
			});
		if (Entries->IsEmpty())
		{
			WaveTurretsByRoom.Remove(RegisteredRoomTag);
		}
	}

	RegisteredWaveTurretRooms.Remove(TurretPtr);
	if (FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RegisteredRoomTag))
	{
		// 전개 중 Actor가 WP에서 내려가도 완료로 세지 않는다. Pending 수량은 그대로 두고
		// 같은 Wave의 교체 Actor가 등록될 때 다시 전개를 요청한다.
		Runtime->PendingWaveTurretActivations.Remove(TurretPtr);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret unregistered. Turret=%s Room=%s"),
		*GetNameSafe(Turret), *RegisteredRoomTag.ToString());
}

bool URoomCombatSubsystem::NotifyWaveTurretDeploymentFinished(AAutoTurret* Turret)
{
	if (!CanRunServerGameplay() || !IsValid(Turret) || !Turret->HasAuthority())
	{
		return false;
	}

	const TWeakObjectPtr<AAutoTurret> TurretPtr(Turret);
	const FGameplayTag* RegisteredRoomTag = RegisteredWaveTurretRooms.Find(TurretPtr);
	FRoomCombatRuntime* Runtime = RegisteredRoomTag
		? RoomRuntimes.Find(*RegisteredRoomTag)
		: nullptr;
	if (!RegisteredRoomTag || !Runtime
		|| !IsActiveCombatRuntime(*RegisteredRoomTag, *Runtime)
		|| Runtime->CurrentCombatPhaseIndex != Turret->GetCombatPhaseIndex()
		|| Runtime->CurrentWaveIndex != Turret->GetWaveIndex()
		|| !Runtime->PendingWaveTurretActivations.Contains(TurretPtr))
	{
		return false;
	}

	if (!RegisterActivatedWaveTurret(*RegisteredRoomTag, *Runtime, Turret))
	{
		return false;
	}

	Runtime->PendingWaveTurretActivations.Remove(TurretPtr);
	Runtime->PendingActivationCount = FMath::Max(Runtime->PendingActivationCount - 1, 0);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret activated. Turret=%s Room=%s Phase=%d Wave=%d PendingActivation=%d"),
		*GetNameSafe(Turret),
		*RegisteredRoomTag->ToString(),
		Runtime->CurrentCombatPhaseIndex,
		Runtime->CurrentWaveIndex,
		Runtime->PendingActivationCount);
	FinalizeCurrentWaveSpawn(*RegisteredRoomTag, *Runtime);
	return true;
}

void URoomCombatSubsystem::GetRegisteredWaveTurrets(
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex,
	TArray<AAutoTurret*>& OutTurrets)
{
	OutTurrets.Reset();
	CompactWaveTurrets(RoomTag);
	const TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RoomTag);
	if (!Entries)
	{
		return;
	}

	for (const FRoomCombatWaveTurretRuntime& Entry : *Entries)
	{
		if (Entry.CombatPhaseIndex == CombatPhaseIndex && Entry.WaveIndex == WaveIndex)
		{
			OutTurrets.Add(Entry.Turret.Get());
		}
	}
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
	FGameplayTag RequiredSpawnPointTag,
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
		if (RequiredSpawnPointTag.IsValid()
			&& !Runtime.SpawnPointTags.HasTag(RequiredSpawnPointTag))
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
	Registration.bPoolManaged = false;

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
	if (Runtime->State == ERoomCombatState::Dormant)
	{
		if (bWasPreplaced && Runtime->bHadPreplacedEnemy
			&& Runtime->TrackedAliveEnemies.IsEmpty())
		{
			// 발각 전에 배치 적을 모두 제거하면 아직 시작하지 않은 후속 Wave를 만들지 않는다.
			CompleteCurrentPhase(RoomTag, true);
		}
		return;
	}

	if (Runtime->State == ERoomCombatState::Combat)
	{
		EvaluateWaveProgress(RoomTag, *Runtime);
	}
}

bool URoomCombatSubsystem::NotifyRoomCombatStarted(FGameplayTag RoomTag)
{
	if (!CanRunServerGameplay() || !RoomTag.IsValid())
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

	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
		: nullptr;
	if (!Phase || Phase->StartPolicy != ERoomCombatPhaseStartPolicy::InitialDetection)
	{
		return false;
	}

	Runtime->State = ERoomCombatState::Combat;
	Runtime->CurrentWaveIndex = 0;
	Runtime->bCurrentWaveSpawnStarted = true;
	ActiveCombatRoomTag = RoomTag;
	SetRoomStreamingSourceEnabled(RoomTag, true);
	// 배치 Wave는 별도 Spawn 요청이 없으므로 전투 진입 자체가 Wave 시작 완료 시점이다.
	FinalizeCurrentWaveSpawn(RoomTag, *Runtime);
	return true;
}

const FRoomCombatWaveDefinition* URoomCombatSubsystem::FindSpawnableWave(
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex) const
{
	if (!CanRunServerGameplay())
	{
		return nullptr;
	}

	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	if (!Runtime || !Definition || !IsActiveCombatRuntime(RoomTag, *Runtime)
		|| Runtime->CurrentCombatPhaseIndex != CombatPhaseIndex)
	{
		return nullptr;
	}

	const FRoomCombatWaveDefinition* Wave = Definition->FindWave(CombatPhaseIndex, WaveIndex);
	if (!Wave || !Wave->IsSpawnFromObjects())
	{
		return nullptr;
	}

	// 같은 Wave의 최초 시작 또는 바로 다음 Wave만 허용한다. 이전 Wave/건너뛰기/중복은 거부한다.
	const bool bStartingCurrentWave = WaveIndex == Runtime->CurrentWaveIndex
		&& !Runtime->bCurrentWaveSpawnStarted;
	const bool bStartingNextWave = WaveIndex == Runtime->CurrentWaveIndex + 1;
	if ((!bStartingCurrentWave && !bStartingNextWave)
		|| HasPendingWaveWork(RoomTag, *Runtime))
	{
		return nullptr;
	}
	return Wave;
}

bool URoomCombatSubsystem::StartWaveSpawning(
	FGameplayTag RoomTag, int32 CombatPhaseIndex, int32 WaveIndex)
{
	const FRoomCombatWaveDefinition* Wave = FindSpawnableWave(RoomTag, CombatPhaseIndex, WaveIndex);
	if (!Wave)
	{
		return false;
	}
	TArray<TSubclassOf<AEnemyBase>> EnemyRoster;
	if (!BuildWaveEnemyRoster(*Wave, RoomTag, CombatPhaseIndex, WaveIndex, EnemyRoster))
	{
		return false;
	}

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const UOutlierArenaSubsystem* ArenaSubsystem =
		GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	const int32 GameplayGeneration = ArenaSubsystem
		? static_cast<int32>(ArenaSubsystem->GetGameplayGeneration())
		: 0;
	// 시작 요청 시점에는 기준값을 비운다. 모든 Pending이 성공한 뒤 기존 생존 적까지 포함해 다시 센다.
	Runtime->CurrentWaveIndex = WaveIndex;
	Runtime->WaveBaselineEnemyCount = INDEX_NONE;
	Runtime->GameplayGeneration = GameplayGeneration;
	Runtime->bCurrentWaveSpawnStarted = true;
	Runtime->SpawnAssignments.Reset();
	QueueWaveSpawnRequests(RoomTag, *Runtime, *Wave, EnemyRoster);
	QueueWaveTurretActivations(RoomTag, *Runtime, *Wave);

	// 해킹 시작/자동 차수 전환은 외부 이벤트 처리 뒤 ResumeDeferredSpawning에서 실행한다.
	if (!Runtime->bDeferSpawnExecution)
	{
		TryStartPendingWaveTurretActivations(RoomTag, *Runtime);
		TrySpawnPendingRequests(RoomTag);
	}
	return true;
}

void URoomCombatSubsystem::QueueWaveTurretActivations(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime,
	const FRoomCombatWaveDefinition& Wave)
{
	Runtime.PendingWaveTurretActivations.Reset();
	Runtime.PendingActivationCount = Wave.ExpectedWaveTurretCount;
	if (Runtime.PendingActivationCount <= 0)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret activation queued. Room=%s Phase=%d Wave=%d Expected=%d Registered=%d"),
		*RoomTag.ToString(),
		Runtime.CurrentCombatPhaseIndex,
		Runtime.CurrentWaveIndex,
		Runtime.PendingActivationCount,
		GetRegisteredWaveTurretCount(
			RoomTag, Runtime.CurrentCombatPhaseIndex, Runtime.CurrentWaveIndex));
}

void URoomCombatSubsystem::TryStartPendingWaveTurretActivations(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	if (!IsActiveCombatRuntime(RoomTag, Runtime)
		|| !Runtime.bCurrentWaveSpawnStarted
		|| Runtime.bDeferSpawnExecution
		|| Runtime.PendingActivationCount <= 0)
	{
		return;
	}

	TArray<AAutoTurret*> WaveTurrets;
	GetRegisteredWaveTurrets(
		RoomTag,
		Runtime.CurrentCombatPhaseIndex,
		Runtime.CurrentWaveIndex,
		WaveTurrets);
	for (AAutoTurret* Turret : WaveTurrets)
	{
		const TWeakObjectPtr<AAutoTurret> TurretPtr(Turret);
		if (!IsValid(Turret)
			|| Runtime.PendingWaveTurretActivations.Contains(TurretPtr))
		{
			continue;
		}

		// BP의 전개 시작 이벤트가 같은 호출 안에서 완료 Notify를 보낼 수도 있으므로
		// 먼저 Pending 소유권을 기록하고 시작 실패 때만 되돌린다.
		Runtime.PendingWaveTurretActivations.Add(TurretPtr);
		if (Turret->BeginRoomWaveDeployment())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[RoomCombat] Wave turret deployment started. Turret=%s Room=%s Phase=%d Wave=%d PendingActivation=%d"),
				*GetNameSafe(Turret),
				*RoomTag.ToString(),
				Runtime.CurrentCombatPhaseIndex,
				Runtime.CurrentWaveIndex,
				Runtime.PendingActivationCount);
		}
		else
		{
			Runtime.PendingWaveTurretActivations.Remove(TurretPtr);
		}
	}

	if (Runtime.PendingWaveTurretActivations.Num() < Runtime.PendingActivationCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomCombat] Wave turret activation remains pending. Room=%s Phase=%d Wave=%d ExpectedRemaining=%d Deploying=%d Registered=%d"),
			*RoomTag.ToString(),
			Runtime.CurrentCombatPhaseIndex,
			Runtime.CurrentWaveIndex,
			Runtime.PendingActivationCount,
			Runtime.PendingWaveTurretActivations.Num(),
			WaveTurrets.Num());
	}
}

bool URoomCombatSubsystem::RegisterActivatedWaveTurret(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime,
	AAutoTurret* Turret)
{
	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Turret);
	if (RegisteredEnemies.Contains(EnemyPtr))
	{
		return false;
	}

	FRoomCombatEnemyRegistration& Registration = RegisteredEnemies.Add(EnemyPtr);
	Registration.RoomTag = RoomTag;
	Registration.CombatPhaseIndex = Runtime.CurrentCombatPhaseIndex;
	Registration.WaveIndex = Runtime.CurrentWaveIndex;
	Registration.GameplayGeneration = Runtime.GameplayGeneration;
	Registration.bPreplaced = false;
	Registration.bPoolManaged = false;
	Runtime.TrackedAliveEnemies.Add(EnemyPtr);
	// StateTree 시작 직후 즉시 사망/상태 이벤트가 와도 Room 등록을 찾을 수 있도록
	// 생존 집계를 먼저 만든 뒤, 활성화 실패 시 두 등록을 함께 되돌린다.
	if (!Turret->CompleteRoomWaveDeployment())
	{
		Runtime.TrackedAliveEnemies.Remove(EnemyPtr);
		RegisteredEnemies.Remove(EnemyPtr);
		return false;
	}
	return true;
}

void URoomCombatSubsystem::QueueWaveSpawnRequests(
	FGameplayTag RoomTag, FRoomCombatRuntime& Runtime,
	const FRoomCombatWaveDefinition& Wave, const TArray<TSubclassOf<AEnemyBase>>& EnemyRoster)
{
	TArray<ARoomCombatSpawnPoint*> EligibleSpawnPoints;
	GetEligibleSpawnPoints(RoomTag, Wave.RequiredSpawnPointTag, EligibleSpawnPoints);
	TArray<float> Weights;
	TArray<int32> AssignedCounts;
	for (ARoomCombatSpawnPoint* SpawnPoint : EligibleSpawnPoints)
	{
		Weights.Add(SpawnPoint->GetSpawnWeight());
		AssignedCounts.Add(0);
	}

	// 후보가 아직 로드되지 않았어도 명단은 Pending에 남긴다. 실제 지점/위치는 재시도에서 확보한다.
	for (int32 RosterIndex = 0; RosterIndex < EnemyRoster.Num(); ++RosterIndex)
	{
		FRoomCombatPendingSpawn& Request = PendingSpawnRequests.AddDefaulted_GetRef();
		Request.EnemyClass = EnemyRoster[RosterIndex];
		Request.RoomTag = RoomTag;
		Request.RequiredSpawnPointTag = Wave.RequiredSpawnPointTag;
		Request.CombatPhaseIndex = Runtime.CurrentCombatPhaseIndex;
		Request.WaveIndex = Runtime.CurrentWaveIndex;
		Request.GameplayGeneration = Runtime.GameplayGeneration;

		const int32 SpawnPointIndex = RoomCombat::SelectWeightedSpawnPoint(
			Weights,
			AssignedCounts,
			RosterIndex);
		if (EligibleSpawnPoints.IsValidIndex(SpawnPointIndex))
		{
			Request.AssignedSpawnPoint = EligibleSpawnPoints[SpawnPointIndex];
			++AssignedCounts[SpawnPointIndex];
			Runtime.SpawnAssignments.FindOrAdd(EligibleSpawnPoints[SpawnPointIndex])++;
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Reinforcement queued. Room=%s Phase=%d Wave=%d Generation=%d Requests=%d SpawnPoints=%d RequiredSpawnPointTag=%s"),
		*RoomTag.ToString(),
		Runtime.CurrentCombatPhaseIndex,
		Runtime.CurrentWaveIndex,
		Runtime.GameplayGeneration,
		EnemyRoster.Num(),
		EligibleSpawnPoints.Num(),
		*Wave.RequiredSpawnPointTag.ToString());
}

bool URoomCombatSubsystem::RegisterSpawnedEnemy(
	AEnemyBase* Enemy,
	const FRoomCombatPendingSpawn& SpawnRequest)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(SpawnRequest.RoomTag);
	if (!IsValid(Enemy) || !Runtime || !MatchesCurrentWave(*Runtime, SpawnRequest))
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
	Registration.bPoolManaged = true;
	// 소환 연출 중에도 이 전투 차수의 생존 적이다. AI 활성 완료 여부와 생존 집계를 분리한다.
	Runtime->TrackedAliveEnemies.Add(EnemyPtr);
	return true;
}

void URoomCombatSubsystem::TrySpawnPendingRequests(FGameplayTag RoomTag)
{
	UWorld* World = GetWorld();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!World || !Runtime || Runtime->State != ERoomCombatState::Combat)
	{
		ScheduleSpawnRetry();
		return;
	}
	if (Runtime->bDeferSpawnExecution)
	{
		return;
	}
	if (!HasPendingSpawns(RoomTag))
	{
		FinalizeCurrentWaveSpawn(RoomTag, *Runtime);
		return;
	}

	UEnemyPoolSubsystem* PoolSubsystem = World->GetSubsystem<UEnemyPoolSubsystem>();
	if (!PoolSubsystem)
	{
		ScheduleSpawnRetry();
		return;
	}

	int32 MissingSpawnPointCount = 0;
	int32 BlockedLocationCount = 0;
	int32 PoolLeaseFailureCount = 0;
	int32 RegistrationFailureCount = 0;
	for (int32 RequestIndex = PendingSpawnRequests.Num() - 1; RequestIndex >= 0; --RequestIndex)
	{
		FRoomCombatPendingSpawn& Request = PendingSpawnRequests[RequestIndex];
		if (Request.RoomTag != RoomTag)
		{
			continue;
		}

		// 이전 수명의 요청만 버린다. 위치 탐색 실패와 Pool 부족은 아래에서 Pending을 유지한다.
		if (!MatchesCurrentWave(*Runtime, Request))
		{
			PendingSpawnRequests.RemoveAtSwap(RequestIndex, 1, EAllowShrinking::No);
			continue;
		}

		TArray<ARoomCombatSpawnPoint*> EligibleSpawnPoints;
		GetEligibleSpawnPoints(RoomTag, Request.RequiredSpawnPointTag, EligibleSpawnPoints);
		ARoomCombatSpawnPoint* SpawnPoint = ResolveSpawnPoint(
			*Runtime,
			Request,
			EligibleSpawnPoints);
		if (!SpawnPoint)
		{
			++MissingSpawnPointCount;
			continue;
		}

		FTransform SpawnTransform;
		const int32 SearchSeed = HashCombineFast(
			GetTypeHash(Request.GameplayGeneration),
			HashCombineFast(GetTypeHash(Request.WaveIndex), ++Request.SearchSerial));
		if (!SpawnPoint->FindSpawnTransform(Request.EnemyClass, SearchSeed, SpawnTransform))
		{
			++BlockedLocationCount;
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
			++PoolLeaseFailureCount;
			continue;
		}

		if (!RegisterSpawnedEnemy(Enemy, Request))
		{
			++RegistrationFailureCount;
			PoolSubsystem->ReturnEnemy(
				Enemy,
				Enemy->GetPoolGameplayGeneration(),
				Enemy->GetPoolLeaseSerial());
			continue;
		}

		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] Reinforcement spawned. Room=%s Phase=%d Wave=%d Enemy=%s SpawnPoint=%s Location=%s Remaining=%d"),
			*RoomTag.ToString(),
			Request.CombatPhaseIndex,
			Request.WaveIndex,
			*GetNameSafe(Enemy),
			*GetNameSafe(SpawnPoint),
			*SpawnTransform.GetLocation().ToCompactString(),
			GetPendingSpawnCount(RoomTag) - 1);

		// Pool 대여와 Room 생존 집계 등록이 모두 성공해야 요청 하나가 완료된다.
		PendingSpawnRequests.RemoveAtSwap(RequestIndex, 1, EAllowShrinking::No);
	}

	if (PendingSpawnRequests.IsEmpty())
	{
		Runtime->SpawnRetryAttempts = 0;
		CancelSpawnRetry();
		FinalizeCurrentWaveSpawn(RoomTag, *Runtime);
		return;
	}

	++Runtime->SpawnRetryAttempts;
	const double CurrentTimeSeconds = World->GetTimeSeconds();
	if (CurrentTimeSeconds - Runtime->LastSpawnRetryLogSeconds >= 5.0)
	{
		Runtime->LastSpawnRetryLogSeconds = CurrentTimeSeconds;
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomCombat] Wave spawn remains pending. Room=%s Phase=%d Wave=%d Pending=%d Attempts=%d MissingSpawnPoint=%d BlockedLocation=%d PoolLeaseFailure=%d RegistrationFailure=%d"),
			*RoomTag.ToString(),
			Runtime->CurrentCombatPhaseIndex,
			Runtime->CurrentWaveIndex,
			GetPendingSpawnCount(RoomTag),
			Runtime->SpawnRetryAttempts,
			MissingSpawnPointCount,
			BlockedLocationCount,
			PoolLeaseFailureCount,
			RegistrationFailureCount);
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

void URoomCombatSubsystem::FinalizeCurrentWaveSpawn(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	if (Runtime.State != ERoomCombatState::Combat
		|| !Runtime.bCurrentWaveSpawnStarted
		|| HasPendingWaveWork(RoomTag, Runtime))
	{
		return;
	}

	CompactAliveEnemies(Runtime);
	Runtime.WaveBaselineEnemyCount = Runtime.TrackedAliveEnemies.Num();
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave spawn completed. Room=%s Phase=%d Wave=%d Baseline=%d"),
		*RoomTag.ToString(),
		Runtime.CurrentCombatPhaseIndex,
		Runtime.CurrentWaveIndex,
		Runtime.WaveBaselineEnemyCount);
	EvaluateWaveProgress(RoomTag, Runtime);
}

void URoomCombatSubsystem::EvaluateWaveProgress(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	// 아직 나오지 못한 적이 있으면 생존 수가 적어도 다음 Wave/차수로 넘기지 않는다.
	if (!IsActiveCombatRuntime(RoomTag, Runtime)
		|| !Runtime.bCurrentWaveSpawnStarted
		|| HasPendingWaveWork(RoomTag, Runtime))
	{
		return;
	}

	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Definition
		? Definition->FindPhase(Runtime.CurrentCombatPhaseIndex)
		: nullptr;
	const FRoomCombatWaveDefinition* Wave = Definition
		? Definition->FindWave(Runtime.CurrentCombatPhaseIndex, Runtime.CurrentWaveIndex)
		: nullptr;
	if (!Phase || !Wave)
	{
		return;
	}

	CompactAliveEnemies(Runtime);
	const int32 AliveEnemyCount = Runtime.TrackedAliveEnemies.Num();
	const bool bLastWave = Runtime.CurrentWaveIndex == Phase->Waves.Num() - 1;
	if (bLastWave)
	{
		// 마지막 Wave는 비율이 아니라 전멸로 완료한다. 앞선 Wave에서 남은 적도 포함된다.
		if (AliveEnemyCount == 0)
		{
			CompleteCurrentPhase(RoomTag, false);
		}
		return;
	}

	if (Runtime.WaveBaselineEnemyCount <= 0)
	{
		return;
	}

	// 분모는 소환 완료 때 확정한 값이다. 사망할 때는 분자만 줄이고 다음 증원 완료 때 갱신한다.
	const float RemainingRatio = static_cast<float>(AliveEnemyCount)
		/ static_cast<float>(Runtime.WaveBaselineEnemyCount);
	const float RequiredRatio = Wave->NextWaveRemainingRatio;
	if (RemainingRatio > RequiredRatio)
	{
		return;
	}

	const int32 NextWaveIndex = Runtime.CurrentWaveIndex + 1;
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Reinforcement triggered. Room=%s Phase=%d CurrentWave=%d NextWave=%d Alive=%d Baseline=%d Ratio=%.3f Required=%.3f"),
		*RoomTag.ToString(),
		Runtime.CurrentCombatPhaseIndex,
		Runtime.CurrentWaveIndex,
		NextWaveIndex,
		AliveEnemyCount,
		Runtime.WaveBaselineEnemyCount,
		RemainingRatio,
		RequiredRatio);
	if (!StartWaveSpawning(RoomTag, Runtime.CurrentCombatPhaseIndex, NextWaveIndex))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Failed to start eligible next Wave. Room=%s Phase=%d Wave=%d Alive=%d Baseline=%d Ratio=%.3f Required=%.3f"),
			*RoomTag.ToString(),
			Runtime.CurrentCombatPhaseIndex,
			NextWaveIndex,
			AliveEnemyCount,
			Runtime.WaveBaselineEnemyCount,
			RemainingRatio,
			RequiredRatio);
	}
}

void URoomCombatSubsystem::ResetRuntimeCombatState()
{
	if (bResettingRuntime)
	{
		return;
	}
	// 취소 통보에서 Reset이 재진입하거나 새 전투를 시작하지 못하게 정리 전체를 하나의 경계로 묶는다.
	TGuardValue<bool> ResetGuard(bResettingRuntime, true);
	struct FCancelledSequence
	{
		FGameplayTag RoomTag;
		int32 PhaseIndex;
		int32 Generation;
	};
	TArray<FCancelledSequence> CancelledSequences;
	TArray<TWeakObjectPtr<AEnemyBase>> EnemiesToReturn;
	for (const auto& Entry : RegisteredEnemies)
	{
		if (Entry.Value.bPoolManaged)
		{
			EnemiesToReturn.Add(Entry.Key);
		}
	}
	CancelSpawnRetry();
	for (const TPair<FGameplayTag, FRoomCombatRuntime>& Entry : RoomRuntimes)
	{
		SetActivationGroupActive(Entry.Key, Entry.Value.ActiveActivationGroupTag, false);
		if (Entry.Value.bTriggeredSequenceActive)
		{
			CancelledSequences.Add({Entry.Key, Entry.Value.CurrentCombatPhaseIndex, Entry.Value.GameplayGeneration});
		}
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

	// Pool 반환도 Room 등록 해제를 호출한다. 역호출이 이전 전투를 진행시키지 않도록 집계부터 비운다.
	RoomRuntimes.Reset();
	PendingPreplacedEnemies.Reset();
	RegisteredEnemies.Reset();
	PendingSpawnRequests.Reset();
	SpawnPointsByRoom.Reset();
	RegisteredSpawnPointRooms.Reset();
	WaveTurretsByRoom.Reset();
	RegisteredWaveTurretRooms.Reset();
	if (UEnemyRoomSubsystem* EnemyRooms = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>() : nullptr)
	{
		EnemyRooms->NotifyRoomCombatEnded(ActiveCombatRoomTag);
	}
	ActiveCombatRoomTag = FGameplayTag();
	if (UEnemyPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UEnemyPoolSubsystem>() : nullptr)
	{
		for (const auto& EnemyPtr : EnemiesToReturn)
		{
			if (AEnemyBase* Enemy = EnemyPtr.Get())
			{
				Pool->ReturnEnemy(Enemy, Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial());
			}
		}
	}
	// 등록 집합을 모두 폐기한 뒤 통보한다. 수신자가 상태를 조회하거나 Reset을 재호출해도 안전하다.
	for (const auto& Sequence : CancelledSequences)
	{
		BroadcastCombatEvent(Sequence.RoomTag, ERoomCombatEvent::Cancelled,
			Sequence.PhaseIndex, Sequence.Generation);
	}
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

int32 URoomCombatSubsystem::GetWaveBaselineEnemyCount(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime ? Runtime->WaveBaselineEnemyCount : INDEX_NONE;
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
		if (IsPendingSpawnForRoom(Request, RoomTag))
		{
			++PendingCount;
		}
	}

	return PendingCount;
}

int32 URoomCombatSubsystem::GetPendingActivationCount(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime ? Runtime->PendingActivationCount : 0;
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

int32 URoomCombatSubsystem::GetRegisteredWaveTurretCount(
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex)
{
	CompactWaveTurrets(RoomTag);
	const TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RoomTag);
	if (!Entries)
	{
		return 0;
	}

	return Entries->CountByPredicate(
		[CombatPhaseIndex, WaveIndex](const FRoomCombatWaveTurretRuntime& Entry)
		{
			return Entry.CombatPhaseIndex == CombatPhaseIndex && Entry.WaveIndex == WaveIndex;
		});
}

void URoomCombatSubsystem::CompleteCurrentPhase(
	FGameplayTag RoomTag,
	bool bCancelRemainingWaves)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* CurrentPhase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
		: nullptr;
	if (!Runtime || !Definition || !CurrentPhase)
	{
		return;
	}
	const int32 CompletedPhaseIndex = Runtime->CurrentCombatPhaseIndex;
	const int32 Generation = Runtime->GameplayGeneration;
	const FGuid RegistrationId = Runtime->RegistrationId;
	const int32 NextPhaseIndex = CompletedPhaseIndex + 1;
	const FRoomCombatPhaseDefinition* NextPhase = Definition->FindPhase(NextPhaseIndex);
	Runtime->WaveBaselineEnemyCount = INDEX_NONE;
	Runtime->PendingActivationCount = 0;
	Runtime->PendingWaveTurretActivations.Reset();
	Runtime->bCurrentWaveSpawnStarted = false;
	Runtime->SpawnAssignments.Reset();

	if (Runtime->bTriggeredSequenceActive && NextPhase)
	{
		// 해킹으로 시작한 연속 전투는 중간 차수가 끝나도 출입 차단과 Streaming을 유지한다.
		StartAutomaticPhase(RoomTag, *Runtime, *Definition);
		return;
	}

	// 연속 전투가 아니거나 마지막 차수면 전투 점유를 해제한 뒤 대기/Cleared 상태로 이동한다.
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

	Runtime->bTriggeredSequenceActive = false;
	SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, false);
	if (!NextPhase)
	{
		MarkRoomCleared(RoomTag, *Runtime);
		// 전체 완료에서 차단 해제를 먼저 알린다. 이어지는 차수 알림이 Reset을 요청해도 해제가 누락되지 않는다.
		BroadcastCombatEvent(RoomTag, ERoomCombatEvent::RoomCleared, CompletedPhaseIndex, Generation);
		// 완료 이벤트 수신 중 WP 해제/Reset이 일어나면 이전 Room의 후속 통보를 보내지 않는다.
		Runtime = RoomRuntimes.Find(RoomTag);
		if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->State == ERoomCombatState::Cleared)
		{
			BroadcastCombatEvent(RoomTag, ERoomCombatEvent::PhaseCompleted, CompletedPhaseIndex, Generation);
		}
		return;
	}

	Runtime->CurrentCombatPhaseIndex = NextPhaseIndex;
	Runtime->CurrentWaveIndex = 0;
	Runtime->State = NextPhase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger
		? ERoomCombatState::WaitingForTrigger
		: ERoomCombatState::Dormant;

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Phase completed. RoomTag=%s Phase=%d CancelledRemainingWaves=%d NextPhase=%d State=%d"),
		*RoomTag.ToString(),
		CompletedPhaseIndex,
		bCancelRemainingWaves ? 1 : 0,
		NextPhaseIndex,
		static_cast<int32>(Runtime->State));
	BroadcastCombatEvent(RoomTag, ERoomCombatEvent::PhaseCompleted, CompletedPhaseIndex, Generation);
}

void URoomCombatSubsystem::StartAutomaticPhase(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime,
	const FRoomCombatRoomDefinition& Definition)
{
	const int32 CompletedPhaseIndex = Runtime.CurrentCombatPhaseIndex;
	const int32 NextPhaseIndex = CompletedPhaseIndex + 1;
	const int32 Generation = Runtime.GameplayGeneration;
	const FGuid RegistrationId = Runtime.RegistrationId;
	const FRoomCombatPhaseDefinition* NextPhase = Definition.FindPhase(NextPhaseIndex);
	Runtime.CurrentCombatPhaseIndex = NextPhaseIndex;
	Runtime.CurrentWaveIndex = 0;
	Runtime.bDeferSpawnExecution = true;

	// 먼저 다음 명단만 준비한다. 외부 완료 이벤트가 Reset을 요청할 수 있어 실제 소환은 뒤로 미룬다.
	if (!NextPhase || NextPhase->StartPolicy != ERoomCombatPhaseStartPolicy::Automatic
		|| !StartWaveSpawning(RoomTag, NextPhaseIndex, 0))
	{
		// 잘못된 런타임 데이터는 완료로 통과시키지 않는다. 출입 차단을 유지하고 원인을 남긴다.
		UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Automatic phase failed. Room=%s Phase=%d Generation=%d"),
			*RoomTag.ToString(), NextPhaseIndex, Generation);
	}
	BroadcastCombatEvent(RoomTag, ERoomCombatEvent::PhaseCompleted, CompletedPhaseIndex, Generation);
	// 콜백 이후 Runtime 참조를 재사용하지 않고, 같은 Room 등록인지 확인한 뒤 소환을 재개한다.
	ResumeDeferredSpawning(RoomTag, RegistrationId);
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

void URoomCombatSubsystem::CompactWaveTurrets(FGameplayTag RoomTag)
{
	TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RoomTag);
	if (!Entries)
	{
		return;
	}

	Entries->RemoveAll(
		[this](const FRoomCombatWaveTurretRuntime& Entry)
		{
			if (Entry.Turret.IsValid())
			{
				return false;
			}

			// EndPlay 해제를 받지 못한 WP Actor도 역방향 인덱스에서 함께 제거한다.
			RegisteredWaveTurretRooms.Remove(Entry.Turret);
			return true;
		});

	if (Entries->IsEmpty())
	{
		WaveTurretsByRoom.Remove(RoomTag);
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
