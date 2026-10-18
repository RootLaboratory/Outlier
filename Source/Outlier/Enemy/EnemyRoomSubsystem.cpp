#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyTargetRules.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"
#include "Team/OutlierTeamIds.h"

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEnemySearchSharing, Log, All);

void LogSearchSharing(const TCHAR* Phase, FGameplayTag RoomTag, const FEnemyRoomTargetContactState& Contact)
{
	static const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	if (!Debug || Debug->GetInt() == 0) return;
	UE_LOG(LogEnemySearchSharing, Log,
		TEXT("[EnemySearchSharing] %s Room=%s Revision=%llu Observers=%d PingConsumed=%d Location=%s Target=%s"),
		Phase, *RoomTag.ToString(), Contact.SharedRevision, Contact.DirectObservers.Num(),
		Contact.bForcedShareConsumed, *Contact.LastReportedLocation.ToCompactString(), *GetNameSafe(Contact.TargetActor.Get()));
}

bool IsSharedTargetObserved(const FEnemyRoomTargetContactState& Contact)
{
	if (OutlierEnemyTargetRules::IsUnavailable(Contact.TargetActor.Get())) return false;
	for (const auto& Observer : Contact.DirectObservers)
	{
		if (Observer.Value == Contact.TargetActor) return true;
	}
	return false;
}
}

void UEnemyRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UOutlierArenaSubsystem* ArenaSubsystem = Collection.InitializeDependency<UOutlierArenaSubsystem>();
	if (ArenaSubsystem)
	{
		ArenaSubsystem->OnArenaReleased.AddUObject(this, &UEnemyRoomSubsystem::HandleArenaReleased);
	}
}

void UEnemyRoomSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<FGameplayTag, FEnemyRoomTargetContactState>& ContactEntry : TargetContactStates)
		{
			World->GetTimerManager().ClearTimer(ContactEntry.Value.ForcedShareTimerHandle);
		}
		if (UOutlierArenaSubsystem* ArenaSubsystem = World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			ArenaSubsystem->OnArenaReleased.RemoveAll(this);
		}
	}

	CombatRooms.Reset();
	CombatRoomLastKnownLocations.Reset();
	RegisteredEnemiesByRoom.Reset();
	RegisteredEnemyKeys.Reset();
	SearchDestinations.Reset();
	TargetContactStates.Reset();
	PatrolDestinations.Reset();
	Super::Deinitialize();
}

void UEnemyRoomSubsystem::GetAutoAimCandidates(TArray<AEnemyBase*>& OutEnemies) const
{
	OutEnemies.Reset();
	for (const auto& Entry : RegisteredEnemyKeys)
	{
		if (AEnemyBase* Enemy = Entry.Key.Get(); IsValid(Enemy) && Enemy->CanBeDamaged()
			&& !Enemy->IsDead() && Enemy->GetCurrentHealth() > 0.0f
			&& Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Enemy)
			&& (!Enemy->IsPoolManaged() || Enemy->GetEnemyPoolState() == EEnemyPoolState::CombatActive)
			&& !Enemy->IsEnemyPossessed() && !Enemy->IsHidden())
		{
			OutEnemies.Add(Enemy);
		}
	}
}

void UEnemyRoomSubsystem::RegisterEnemy(AEnemyBase* Enemy)
{
	if (IsValid(Enemy) && Enemy->HasAuthority())
	{
		RefreshEnemyRegistration(Enemy);
		SynchronizeEnemyWithRoomState(Enemy);
	}
}

void UEnemyRoomSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	ReleaseSearchDestinations(Enemy);
	ReleasePatrolDestination(Enemy);
	if (!Enemy)
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyKey(Enemy);
	if (const FGameplayTag* RegisteredKey = RegisteredEnemyKeys.Find(EnemyKey))
	{
		if (TSet<TWeakObjectPtr<AEnemyBase>>* Enemies = RegisteredEnemiesByRoom.Find(*RegisteredKey))
		{
			Enemies->Remove(EnemyKey);
			if (Enemies->IsEmpty())
			{
				RegisteredEnemiesByRoom.Remove(*RegisteredKey);
			}
		}
		RegisteredEnemyKeys.Remove(EnemyKey);
	}
}

void UEnemyRoomSubsystem::RefreshEnemyRegistration(AEnemyBase* Enemy)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority())
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	const FGameplayTag NewKey = ResolveEnemyRegistrationKey(Enemy);
	if (!NewKey.IsValid())
	{
		UnregisterEnemy(Enemy);
		return;
	}

	if (const FGameplayTag* PreviousKey = RegisteredEnemyKeys.Find(EnemyPtr))
	{
		if (*PreviousKey == NewKey)
		{
			return;
		}

		if (TSet<TWeakObjectPtr<AEnemyBase>>* PreviousEnemies =
			RegisteredEnemiesByRoom.Find(*PreviousKey))
		{
			PreviousEnemies->Remove(EnemyPtr);
			if (PreviousEnemies->IsEmpty())
			{
				RegisteredEnemiesByRoom.Remove(*PreviousKey);
			}
		}
	}

	RegisteredEnemiesByRoom.FindOrAdd(NewKey).Add(EnemyPtr);
	RegisteredEnemyKeys.Add(EnemyPtr, NewKey);
}

bool UEnemyRoomSubsystem::NotifyRoomCombat(FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !RoomTag.IsValid())
	{
		return false;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!ArenaSubsystem)
	{
		return false;
	}

	ULevel* ArenaLevel = ArenaSubsystem->GetArenaLoadedLevel();
	if (!ArenaLevel)
	{
		return false;
	}

	if (URoomCombatSubsystem* CombatSubsystem =
		World->GetSubsystem<URoomCombatSubsystem>())
	{
		// 전투 정의가 있는 방은 새 관리자의 단일 활성 Room 판정을 통과한 경우에만 AI 전파한다.
		if (CombatSubsystem->IsRoomRegistered(RoomTag)
			&& !CombatSubsystem->TryStartInitialDetectionForPlayers(RoomTag))
		{
			return false;
		}
	}
	CombatRooms.Add(RoomTag);
	CombatRoomLastKnownLocations.Add(RoomTag, PlayerLocation);

	CompactRegisteredEnemies(RoomTag);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(RoomTag);
	if (RegisteredEnemies)
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
		{
			AEnemyBase* Enemy = EnemyPtr.Get();
			if (!IsValid(Enemy) || Enemy == ExcludeEnemy)
			{
				continue;
			}

			if (Enemy->IsPoolManaged())
			{
				// 대기/등장/사망 연출 중인 풀 개체는 완료 콜백에서 따로 동기화한다.
				if (Enemy->GetEnemyPoolState() != EEnemyPoolState::CombatActive
					|| Enemy->IsDead() || Enemy->IsAIControlSuppressed()
					|| !Enemy->CanUseRoomTargetSharing())
				{
					continue;
				}
			}
			Enemy->EnterCombatFromRoom(PlayerLocation, false);
		}
	}

	if (const FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
		ContactState && ContactState->bSharedContactActive)
	{
		// 전투 합류 때 기존 정보를 재전달할 뿐, 새 목격 회차를 만들지는 않는다.
		BroadcastSharedTargetContact(RoomTag, ContactState->LastReportedLocation, false);
	}
	return true;
}

void UEnemyRoomSubsystem::NotifyRoomCombatEnded(FGameplayTag RoomTag)
{
	// 여기서는 AI 공유 정보만 해제한다. Wave 완료/다음 차수 판정은 RoomCombatSubsystem이 소유한다.
	if (!RoomTag.IsValid())
	{
		return;
	}

	CombatRooms.Remove(RoomTag);
	CombatRoomLastKnownLocations.Remove(RoomTag);
	for (auto It = SearchDestinations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key()->GetDefaultRoomTag() == RoomTag) It.RemoveCurrent();
	}
	if (FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ContactState->ForcedShareTimerHandle);
		}
		TargetContactStates.Remove(RoomTag);
	}
}

bool UEnemyRoomSubsystem::IsRoomInCombat(FGameplayTag RoomTag) const
{
	if (!RoomTag.IsValid())
	{
		return false;
	}
	return CombatRooms.Contains(RoomTag);
}

bool UEnemyRoomSubsystem::HasActiveCombat() const
{
	for (const TPair<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>>& RoomEntry :
		RegisteredEnemiesByRoom)
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RoomEntry.Value)
		{
			const AEnemyBase* Enemy = EnemyPtr.Get();
			if (IsValid(Enemy) && !Enemy->IsDead() && Enemy->IsInCombat())
			{
				return true;
			}
		}
	}

	return false;
}

void UEnemyRoomSubsystem::ReportRoomTargetContact(
	AEnemyBase* Observer,
	AActor* TargetActor,
	const FVector& TargetLocation)
{
	UWorld* World = GetWorld();
	if (!World
		|| World->GetNetMode() == NM_Client
		|| !IsValid(Observer)
		|| !IsValid(TargetActor)
		|| !Observer->HasAuthority()
		|| !Observer->CanUseRoomTargetSharing())
	{
		return;
	}

	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Observer);
	if (!RoomTag.IsValid())
	{
		return;
	}

	FEnemyRoomTargetContactState& ContactState = TargetContactStates.FindOrAdd(RoomTag);
	CompactTargetContactState(ContactState);
	// 다른 드론이 계속 보고 있어도, 시야를 잃었던 개체의 실제 재목격은 새 정보다.
	const TWeakObjectPtr<AActor>* PreviousTarget = ContactState.DirectObservers.Find(Observer);
	const bool bRediscovered = !PreviousTarget || PreviousTarget->Get() != TargetActor;
	World->GetTimerManager().ClearTimer(ContactState.ForcedShareTimerHandle);
	// 직접 재발견은 다음 시야 상실 회차의 단발 핑을 다시 허용한다.
	ContactState.bForcedShareConsumed = false;
	ContactState.DirectObservers.Add(TWeakObjectPtr<AEnemyBase>(Observer), TWeakObjectPtr<AActor>(TargetActor));

	const bool bTargetChanged = ContactState.TargetActor.Get() != TargetActor;
	// 관측 등록과 핑 초기화는 모든 타깃에 적용하되, 공유 타깃 교체는 우선순위로 제한한다.
	// 같은 우선순위에서는 현재 타깃의 관측이 끊길 때까지 유지해 보고 순서에 따른 왕복을 막는다.
	if (bTargetChanged && IsSharedTargetObserved(ContactState)
		&& OutlierEnemyTargetRules::GetPriority(TargetActor)
			>= OutlierEnemyTargetRules::GetPriority(ContactState.TargetActor.Get()))
	{
		return;
	}
	const bool bLocationChanged = FVector::DistSquared(
		ContactState.LastReportedLocation,
		TargetLocation) > FMath::Square(50.0f);

	ContactState.TargetActor = TargetActor;
	if (!bRediscovered && !bTargetChanged && !bLocationChanged && ContactState.bSharedContactActive)
	{
		return;
	}

	ContactState.LastReportedLocation = TargetLocation;
	BroadcastSharedTargetContact(RoomTag, TargetLocation);
}

void UEnemyRoomSubsystem::RemoveRoomTargetObserver(AEnemyBase* Observer)
{
	if (!Observer)
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> ObserverKey(Observer);
	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		FEnemyRoomTargetContactState& ContactState = ContactIt.Value();
		const bool bRemovedDirectObserver =
			ContactState.DirectObservers.Remove(ObserverKey) > 0;
		CompactTargetContactState(ContactState);
		if (!bRemovedDirectObserver || !ContactState.DirectObservers.IsEmpty())
		{
			continue;
		}

		BroadcastSharedTargetLost(ContactIt.Key());
		ScheduleForcedTargetShare(ContactIt.Key());
	}
}

void UEnemyRoomSubsystem::ScheduleForcedTargetShare(FGameplayTag RoomTag)
{
	UWorld* World = GetWorld();
	FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
	if (!World || World->GetNetMode() == NM_Client || !ContactState
		|| !IsRoomInCombat(RoomTag) || ContactState->bForcedShareConsumed
		|| !ContactState->DirectObservers.IsEmpty() || !ContactState->TargetActor.IsValid())
	{
		return;
	}

	World->GetTimerManager().ClearTimer(ContactState->ForcedShareTimerHandle);
	World->GetTimerManager().SetTimer(
		ContactState->ForcedShareTimerHandle,
		FTimerDelegate::CreateUObject(this, &UEnemyRoomSubsystem::HandleForcedTargetShare, RoomTag),
		FMath::Max(ForcedTargetShareDelay, KINDA_SMALL_NUMBER),
		false);
	LogSearchSharing(TEXT("AllSightLost:Scheduled180Seconds"), RoomTag, *ContactState);
}

void UEnemyRoomSubsystem::HandleForcedTargetShare(FGameplayTag RoomTag)
{
	FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
	if (!ContactState || !IsRoomInCombat(RoomTag) || ContactState->bForcedShareConsumed)
	{
		return;
	}
	CompactTargetContactState(*ContactState);
	if (!ContactState->DirectObservers.IsEmpty())
	{
		return;
	}

	AActor* TargetActor = ContactState->TargetActor.Get();
	if (!IsValid(TargetActor))
	{
		TargetContactStates.Remove(RoomTag);
		return;
	}

	if (OutlierEnemyTargetRules::IsUnavailable(TargetActor))
	{
		// 타겟 불가 상태의 현재 위치는 공개하지 않는다. 유효해진 뒤 새 대기 시간을 적용한다.
		ScheduleForcedTargetShare(RoomTag);
		return;
	}

	ContactState->LastReportedLocation = TargetActor->GetActorLocation();
	ContactState->bForcedShareConsumed = true;
	LogSearchSharing(TEXT("ForcedPingOnce"), RoomTag, *ContactState);
	BroadcastSharedTargetContact(RoomTag, ContactState->LastReportedLocation);
}

void UEnemyRoomSubsystem::NotifyTargetActorRemoved(AActor* TargetActor)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !TargetActor)
	{
		return;
	}

	TArray<FGameplayTag> RemovedContactKeys;
	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		if (ContactIt.Value().TargetActor.Get() == TargetActor
			|| !ContactIt.Value().TargetActor.IsValid())
		{
			World->GetTimerManager().ClearTimer(ContactIt.Value().ForcedShareTimerHandle);
			RemovedContactKeys.Add(ContactIt.Key());
			ContactIt.RemoveCurrent();
		}
	}

	for (const FGameplayTag& Key : RemovedContactKeys)
	{
		BroadcastSharedTargetLost(Key);
	}

	RefreshDetectionTarget(TargetActor);
}

void UEnemyRoomSubsystem::RefreshDetectionTarget(AActor* TargetActor)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(TargetActor))
	{
		return;
	}

	CompactAllRegisteredEnemies();
	for (const TPair<FGameplayTag, TSet<TWeakObjectPtr<AEnemyBase>>>& RoomEntry :
		RegisteredEnemiesByRoom)
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RoomEntry.Value)
		{
			AEnemyBase* Enemy = EnemyPtr.Get();
			AEnemyAIController* AIController = IsValid(Enemy)
				? Cast<AEnemyAIController>(Enemy->GetController())
				: nullptr;
			if (AIController)
			{
				AIController->ForgetDetectionTarget(TargetActor);
			}
		}
	}
}


void UEnemyRoomSubsystem::BroadcastSharedTargetContact(
	FGameplayTag RoomTag,
	const FVector& TargetLocation,
	bool bNewInformation)
{
	if (CombatRooms.Contains(RoomTag))
	{
		CombatRoomLastKnownLocations.Add(RoomTag, TargetLocation);
	}
	if (FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag))
	{
		ContactState->bSharedContactActive = true;
		if (bNewInformation || ContactState->SharedRevision == 0)
		{
			ContactState->SharedRevision = ++NextSharedRevision;
		}
		LogSearchSharing(TEXT("Publish"), RoomTag, *ContactState);
	}

	UWorld* World = GetWorld();
	UOutlierArenaSubsystem* ArenaSubsystem = World
		? World->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaSubsystem ? ArenaSubsystem->GetArenaLoadedLevel() : nullptr;
	if (!ArenaLevel)
	{
		return;
	}

	CompactRegisteredEnemies(RoomTag);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(RoomTag);
	if (!RegisteredEnemies)
	{
		return;
	}

	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy) || !Enemy->CanUseRoomTargetSharing())
		{
			continue;
		}

		if (Enemy->IsPoolManaged())
		{
			if (Enemy->GetEnemyPoolState() != EEnemyPoolState::CombatActive
				|| Enemy->IsDead() || Enemy->IsAIControlSuppressed())
			{
				continue;
			}
			// 전투 중인 방의 공유는 직접 감지를 대신하는 합류 조건이다.
			// 첫 발각의 Alert 확인이 끝나기 전에는 방 전투를 앞서 시작하지 않는다.
			if (CombatRooms.Contains(RoomTag))
			{
				Enemy->EnterCombatFromRoom(TargetLocation, false);
			}
			else if (Enemy->GetCombatState() != EEnemyCombatState::Combat
				&& Enemy->GetCombatState() != EEnemyCombatState::Alert)
			{
				continue;
			}
		}
		else if (Enemy->GetCombatState() != EEnemyCombatState::Combat)
		{
			continue;
		}

		const FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
		Enemy->ApplySharedTargetContact(TargetLocation, false, ContactState ? ContactState->SharedRevision : 0);
	}
}

void UEnemyRoomSubsystem::BroadcastSharedTargetLost(FGameplayTag RoomTag)
{
	if (FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag))
	{
		ContactState->bSharedContactActive = false;
	}

	UWorld* World = GetWorld();
	UOutlierArenaSubsystem* ArenaSubsystem = World
		? World->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaSubsystem ? ArenaSubsystem->GetArenaLoadedLevel() : nullptr;
	if (!ArenaLevel)
	{
		return;
	}

	CompactRegisteredEnemies(RoomTag);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(RoomTag);
	if (!RegisteredEnemies)
	{
		return;
	}

	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy))
		{
			continue;
		}

		Enemy->ClearSharedTargetContact();
	}
}

void UEnemyRoomSubsystem::CompactTargetContactState(FEnemyRoomTargetContactState& ContactState)
{
	for (auto ObserverIt = ContactState.DirectObservers.CreateIterator(); ObserverIt; ++ObserverIt)
	{
		if (!ObserverIt.Key().IsValid() || !ObserverIt.Value().IsValid())
		{
			ObserverIt.RemoveCurrent();
		}
	}
}

void UEnemyRoomSubsystem::SynchronizeEnemyWithRoomState(AEnemyBase* Enemy)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority())
	{
		return;
	}

	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Enemy);
	const FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
	if (!CombatRooms.Contains(RoomTag))
	{
		if (Enemy->IsPoolManaged())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[Reinforcement] Enemy=%s Stage=RoomSyncSkipped Generation=%d Lease=%d Room=%s Reason=RoomNotInCombat"),
				*GetNameSafe(Enemy), Enemy->GetPoolGameplayGeneration(),
				Enemy->GetPoolLeaseSerial(), *RoomTag.ToString());
		}
		return;
	}

	const FVector* LastKnownLocation = CombatRoomLastKnownLocations.Find(RoomTag);
	if (!ContactState && !LastKnownLocation)
	{
		if (Enemy->IsPoolManaged())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[Reinforcement] Enemy=%s Stage=RoomSyncSkipped Generation=%d Lease=%d Room=%s Reason=NoTargetLocation"),
				*GetNameSafe(Enemy), Enemy->GetPoolGameplayGeneration(),
				Enemy->GetPoolLeaseSerial(), *RoomTag.ToString());
		}
		return;
	}
	// 새 증원도 방의 마지막 전투 위치를 받아 합류한다.
	// 현재 공유가 끊겼다면 직접 시야를 만들지 않고 마지막 위치를 수색한다.
	const FVector TargetLocation = ContactState
		? ContactState->LastReportedLocation : *LastKnownLocation;
	Enemy->EnterCombatFromRoom(TargetLocation, false, true);
	if (ContactState && ContactState->bSharedContactActive)
	{
		Enemy->ApplySharedTargetContact(TargetLocation, true, ContactState->SharedRevision);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UEnemyRoomSubsystem::SetActiveRoomTargetForTesting(
	FGameplayTag RoomTag,
	const FVector& TargetLocation,
	bool bSharedContactActive)
{
	CombatRooms.Add(RoomTag);
	CombatRoomLastKnownLocations.Add(RoomTag, TargetLocation);
	FEnemyRoomTargetContactState& ContactState = TargetContactStates.FindOrAdd(RoomTag);
	ContactState.LastReportedLocation = TargetLocation;
	ContactState.bSharedContactActive = bSharedContactActive;
	ContactState.SharedRevision = ++NextSharedRevision;
}

uint64 UEnemyRoomSubsystem::GetSharedRevisionForTesting(FGameplayTag RoomTag) const
{
	const FEnemyRoomTargetContactState* Contact = TargetContactStates.Find(RoomTag);
	return Contact ? Contact->SharedRevision : 0;
}

bool UEnemyRoomSubsystem::IsForcedShareConsumedForTesting(FGameplayTag RoomTag) const
{
	const FEnemyRoomTargetContactState* Contact = TargetContactStates.Find(RoomTag);
	return Contact && Contact->bForcedShareConsumed;
}
#endif

bool UEnemyRoomSubsystem::IsSearchDestinationReserved(
	const AEnemyBase* Enemy, const FVector& Location, float Separation) const
{
	if (!IsValid(Enemy)) return true;
	const FGameplayTag RoomTag = Enemy->GetDefaultRoomTag();
	for (const auto& Entry : SearchDestinations)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		if (!Other || Other == Enemy || Other->IsDead() || !Other->GetSearchEpisode().bActive
			|| Other->GetSearchEpisode().bLocalGuard
			|| Other->GetDefaultRoomTag() != RoomTag) continue;
		for (const FVector& Point : Entry.Value)
		{
			if (FVector::DistSquared(Location, Point) < FMath::Square(Separation)) return true;
		}
	}
	return false;
}

bool UEnemyRoomSubsystem::ReserveSearchDestinations(
	AEnemyBase* Enemy, const TArray<FVector>& Locations, float Separation)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->GetSearchEpisode().bActive
		|| Enemy->GetSearchEpisode().bLocalGuard
		|| Locations.Num() > 2 || !FMath::IsFinite(Separation) || Separation <= 0.0f) return false;
	// 결과 적용 시 다시 검사한다. 먼저 끝난 질의가 같은 위치를 예약했을 수 있다.
	for (const FVector& Point : Locations)
	{
		if (Point.ContainsNaN() || IsSearchDestinationReserved(Enemy, Point, Separation)) return false;
	}
	for (auto It = SearchDestinations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	SearchDestinations.Add(TWeakObjectPtr<AEnemyBase>(Enemy), Locations);
	return true;
}

void UEnemyRoomSubsystem::ReleaseSearchDestinations(AEnemyBase* Enemy)
{
	if (Enemy) SearchDestinations.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
}

namespace
{
bool IsCurrentPatrolReservation(const AEnemyBase* Enemy, const FEnemyPatrolDestination& Reservation)
{
	return IsValid(Enemy) && Enemy->HasAuthority() && Enemy->CanUseEnemyPerception()
		&& !Enemy->IsDead() && !Enemy->IsAIControlSuppressed()
		&& Enemy->GetCombatState() == EEnemyCombatState::NonCombat
		&& Enemy->GetNonCombatBehavior() == EEnemyNonCombatBehavior::PatrolRoute
		&& Enemy->GetController() && Enemy->GetController() == Reservation.Controller.Get()
		&& Enemy->GetDefaultRoomTag() == Reservation.RoomTag
		&& Enemy->GetPoolGameplayGeneration() == Reservation.Generation
		&& Enemy->GetPoolLeaseSerial() == Reservation.Lease;
}
}

bool UEnemyRoomSubsystem::IsPatrolDestinationUsed(
	const AEnemyBase* Enemy, const FVector& Location, float Separation) const
{
	if (!IsValid(Enemy)) return false;
	for (const auto& Entry : PatrolDestinations)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		if (Other == Enemy || !IsCurrentPatrolReservation(Other, Entry.Value)
			|| Entry.Value.RoomTag != Enemy->GetDefaultRoomTag()) continue;
		if (FVector::DistSquared(Location, Entry.Value.Location) < FMath::Square(Separation)) return true;
	}
	return false;
}

bool UEnemyRoomSubsystem::ReservePatrolDestination(AEnemyBase* Enemy, const FVector& Location)
{
	if (!IsValid(Enemy) || Location.ContainsNaN()) return false;
	FEnemyPatrolDestination Reservation;
	Reservation.Location = Location;
	Reservation.RoomTag = Enemy->GetDefaultRoomTag();
	Reservation.Controller = Enemy->GetController();
	Reservation.Generation = Enemy->GetPoolGameplayGeneration();
	Reservation.Lease = Enemy->GetPoolLeaseSerial();
	if (!IsCurrentPatrolReservation(Enemy, Reservation)) return false;
	for (auto It = PatrolDestinations.CreateIterator(); It; ++It)
	{
		if (!IsCurrentPatrolReservation(It.Key().Get(), It.Value())) It.RemoveCurrent();
	}
	// 순찰은 공유 목적지를 금지하지 않는다. 대체 지점이 없으면 같은 지점도 사용할 수 있다.
	PatrolDestinations.Add(TWeakObjectPtr<AEnemyBase>(Enemy), Reservation);
	return true;
}

void UEnemyRoomSubsystem::ReleasePatrolDestination(AEnemyBase* Enemy)
{
	if (Enemy) PatrolDestinations.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
}

void UEnemyRoomSubsystem::ReleasePatrolDestination(AEnemyBase* Enemy, int32 Generation, int32 Lease, AController* Controller)
{
	const FEnemyPatrolDestination* Reservation = PatrolDestinations.Find(Enemy);
	if (Reservation && Reservation->Generation == Generation && Reservation->Lease == Lease
		&& Reservation->Controller.Get() == Controller)
	{
		ReleasePatrolDestination(Enemy);
	}
}

FGameplayTag UEnemyRoomSubsystem::ResolveEnemyRoomTag(const AEnemyBase* Enemy) const
{
	if (!Enemy)
	{
		return FGameplayTag();
	}

	return Enemy->GetDefaultRoomTag();
}

FGameplayTag UEnemyRoomSubsystem::ResolveEnemyRegistrationKey(
	const AEnemyBase* Enemy) const
{
	if (!Enemy)
	{
		return FGameplayTag();
	}
	return ResolveEnemyRoomTag(Enemy);
}

void UEnemyRoomSubsystem::CompactRegisteredEnemies(FGameplayTag RoomTag)
{
	TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(RoomTag);
	if (!RegisteredEnemies)
	{
		return;
	}

	for (auto EnemyIt = RegisteredEnemies->CreateIterator(); EnemyIt; ++EnemyIt)
	{
		if (!(*EnemyIt).IsValid())
		{
			RegisteredEnemyKeys.Remove(*EnemyIt);
			EnemyIt.RemoveCurrent();
		}
	}

	if (RegisteredEnemies->IsEmpty())
	{
		RegisteredEnemiesByRoom.Remove(RoomTag);
	}
}

void UEnemyRoomSubsystem::CompactAllRegisteredEnemies()
{
	for (auto RoomIt = RegisteredEnemiesByRoom.CreateIterator(); RoomIt; ++RoomIt)
	{
		for (auto EnemyIt = RoomIt.Value().CreateIterator(); EnemyIt; ++EnemyIt)
		{
			if (!(*EnemyIt).IsValid())
			{
				RegisteredEnemyKeys.Remove(*EnemyIt);
				EnemyIt.RemoveCurrent();
			}
		}

		if (RoomIt.Value().IsEmpty())
		{
			RoomIt.RemoveCurrent();
		}
	}
}

void UEnemyRoomSubsystem::HandleArenaReleased()
{
	ResetRuntimeCombatState();
}

void UEnemyRoomSubsystem::ResetRuntimeCombatState()
{
	SearchDestinations.Reset();
	PatrolDestinations.Reset();
	UWorld* World = GetWorld();
	CombatRooms.Reset();
	CombatRoomLastKnownLocations.Reset();
	RegisteredEnemiesByRoom.Reset();
	RegisteredEnemyKeys.Reset();

	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ContactIt.Value().ForcedShareTimerHandle);
		}
	}
	TargetContactStates.Reset();
}
