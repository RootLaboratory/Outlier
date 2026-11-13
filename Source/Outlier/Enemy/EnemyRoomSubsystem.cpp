#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyTargetRules.h"
#include "Enemy/VECDrone.h"
#include "Components/CapsuleComponent.h"
#include "Drone/FlightPathfinding.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interface/GameplayTagProviderInterface.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"
#include "Team/OutlierTeamIds.h"
#include "Weapon/LaserWeapon.h"

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogEnemySearchSharing, Log, All);

void LogSearchSharing(const TCHAR* Phase, FGameplayTag RoomTag, const FEnemyRoomTargetContactState& Contact)
{
	static const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	if (!Debug || Debug->GetInt() == 0)
	{
		return;
	}
	UE_LOG(LogEnemySearchSharing, Log,
		TEXT("[EnemySearchSharing] %s Room=%s Revision=%llu Observers=%d PingConsumed=%d Location=%s Target=%s"),
		Phase, *RoomTag.ToString(), Contact.SharedRevision, Contact.DirectObservers.Num(),
		Contact.bForcedShareConsumed, *Contact.LastReportedLocation.ToCompactString(), *GetNameSafe(Contact.TargetActor.Get()));
}

bool IsObservationTargetUnavailable(const AActor* Target)
{
	if (OutlierEnemyTargetRules::IsUnavailable(Target) || Target->IsActorBeingDestroyed())
	{
		return true;
	}
	// Enemy는 GameplayTagProvider 대신 ASC로 상태를 제공한다. 빙의 대상도 같은 경계로 검사한다.
	const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(Target);
	const UAbilitySystemComponent* AbilitySystem = AbilityOwner ? AbilityOwner->GetAbilitySystemComponent() : nullptr;
	if (AbilitySystem && (AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
		|| AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed())
		|| AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting())))
	{
		return true;
	}
	const IGameplayTagProviderInterface* Tags = Cast<IGameplayTagProviderInterface>(Target);
	return Tags && Tags->GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::State::Dead());
}

bool IsSharedTargetObserved(const FEnemyRoomTargetContactState& Contact)
{
	if (OutlierEnemyTargetRules::IsUnavailable(Contact.TargetActor.Get()))
	{
		return false;
	}
	for (const auto& Observer : Contact.DirectObservers)
	{
		if (Observer.Value.TargetActor == Contact.TargetActor)
		{
			return true;
		}
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
	CombatDestinations.Reset();
	CombatReservationRequests.Reset();
	CombatYieldRequests.Reset();
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
	ReleaseCombatReservation(Enemy);
	ReleaseSearchDestinations(Enemy);
	ReleasePatrolDestination(Enemy);
	RemoveRoomTargetObserver(Enemy);
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

		RemoveRoomTargetObserver(Enemy);
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
		if (!It.Key().IsValid() || It.Key()->GetDefaultRoomTag() == RoomTag)
		{
			It.RemoveCurrent();
		}
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

void UEnemyRoomSubsystem::GetActiveCombatEnemies(TArray<AEnemyBase*>& OutEnemies) const
{
	OutEnemies.Reset();
	for (const auto& Room : RegisteredEnemiesByRoom)
	{
		for (const TWeakObjectPtr<AEnemyBase>& Entry : Room.Value)
		{
			AEnemyBase* Enemy = Entry.Get();
			if (IsValid(Enemy) && !Enemy->IsDead() && Enemy->IsInCombat())
			{
				OutEnemies.Add(Enemy);
			}
		}
	}
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
		|| !Observer->CanUseRoomTargetSharing()
		|| Observer->IsDead() || !Observer->CanUseEnemyPerception()
		|| Observer->GetCombatState() == EEnemyCombatState::Stun
		|| TargetLocation.ContainsNaN()
		|| IsObservationTargetUnavailable(TargetActor))
	{
		return;
	}

	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Observer);
	if (!RoomTag.IsValid())
	{
		return;
	}

	FEnemyRoomTargetContactState& ContactState = TargetContactStates.FindOrAdd(RoomTag);
	CompactTargetContactState(RoomTag, ContactState);
	// 다른 드론이 계속 보고 있어도, 시야를 잃었던 개체의 실제 재목격은 새 정보다.
	const FEnemyRoomDirectObserver* PreviousObserver = ContactState.DirectObservers.Find(Observer);
	const bool bRediscovered = !PreviousObserver || PreviousObserver->TargetActor.Get() != TargetActor;
	World->GetTimerManager().ClearTimer(ContactState.ForcedShareTimerHandle);
	// 직접 재발견은 다음 시야 상실 회차의 단발 핑을 다시 허용한다.
	ContactState.bForcedShareConsumed = false;
	FEnemyRoomDirectObserver& DirectObserver = ContactState.DirectObservers.FindOrAdd(Observer);
	DirectObserver.TargetActor = TargetActor;
	DirectObserver.Controller = Observer->GetController();
	DirectObserver.Generation = Observer->GetPoolGameplayGeneration();
	DirectObserver.Lease = Observer->GetPoolLeaseSerial();
	// 좌표가 50cm 미만으로 변했거나 대표 대상이 아니어도 실제 관측 시각은 갱신한다.
	// 아래의 기존 좌표 공유 조기 반환과 강제 핑은 이 기록을 변경하지 않는다.
	FEnemyRoomTargetObservation& Observation = ContactState.TargetObservations.FindOrAdd(TargetActor);
	Observation.LastObservedLocation = TargetLocation;
	Observation.LastObservedAt = World->GetTimeSeconds();

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
		CompactTargetContactState(ContactIt.Key(), ContactState);
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
	CompactTargetContactState(RoomTag, *ContactState);
	if (!ContactState->DirectObservers.IsEmpty())
	{
		return;
	}

	AActor* TargetActor = ContactState->TargetActor.Get();
	if (!IsValid(TargetActor))
	{
		ContactState->TargetActor.Reset();
		ContactState->bSharedContactActive = false;
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
		ContactIt.Value().TargetObservations.Remove(TargetActor);
		for (auto ObserverIt = ContactIt.Value().DirectObservers.CreateIterator(); ObserverIt; ++ObserverIt)
		{
			if (ObserverIt.Value().TargetActor.Get() == TargetActor)
			{
				ObserverIt.RemoveCurrent();
			}
		}
		if (ContactIt.Value().TargetActor.Get() == TargetActor
			|| !ContactIt.Value().TargetActor.IsValid())
		{
			World->GetTimerManager().ClearTimer(ContactIt.Value().ForcedShareTimerHandle);
			RemovedContactKeys.Add(ContactIt.Key());
			ContactIt.Value().TargetActor.Reset();
			ContactIt.Value().bSharedContactActive = false;
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
			if (CombatRooms.Contains(RoomTag) && Enemy->GetCombatState() != EEnemyCombatState::Returning)
			{
				Enemy->EnterCombatFromRoom(TargetLocation, false);
			}
			else if (Enemy->GetCombatState() != EEnemyCombatState::Combat
				&& Enemy->GetCombatState() != EEnemyCombatState::Returning
				&& Enemy->GetCombatState() != EEnemyCombatState::Alert)
			{
				continue;
			}
		}
		else if (Enemy->GetCombatState() != EEnemyCombatState::Combat
			&& Enemy->GetCombatState() != EEnemyCombatState::Returning)
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

void UEnemyRoomSubsystem::CompactTargetContactState(
	FGameplayTag RoomTag, FEnemyRoomTargetContactState& ContactState)
{
	for (auto ObserverIt = ContactState.DirectObservers.CreateIterator(); ObserverIt; ++ObserverIt)
	{
		const AEnemyBase* Observer = ObserverIt.Key().Get();
		const FEnemyRoomDirectObserver& Report = ObserverIt.Value();
		if (!IsValid(Observer) || !Observer->CanUseRoomTargetSharing()
			|| Observer->IsDead() || !Observer->CanUseEnemyPerception()
			|| Observer->GetCombatState() == EEnemyCombatState::Stun
			|| ResolveEnemyRoomTag(Observer) != RoomTag
			|| Observer->GetController() != Report.Controller.Get()
			|| Observer->GetPoolGameplayGeneration() != Report.Generation
			|| Observer->GetPoolLeaseSerial() != Report.Lease
			|| IsObservationTargetUnavailable(Report.TargetActor.Get()))
		{
			ObserverIt.RemoveCurrent();
		}
	}
	for (auto TargetIt = ContactState.TargetObservations.CreateIterator(); TargetIt; ++TargetIt)
	{
		if (IsObservationTargetUnavailable(TargetIt.Key().Get()))
		{
			TargetIt.RemoveCurrent();
		}
	}
}

bool UEnemyRoomSubsystem::TryGetRoomTargetObservation(
	AEnemyBase* Receiver, AActor* TargetActor, FEnemyRoomTargetObservationSnapshot& OutSnapshot)
{
	OutSnapshot = {};
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(Receiver)
		|| !Receiver->HasAuthority() || !Receiver->CanUseRoomTargetSharing()
		|| Receiver->IsDead() || !Receiver->CanUseEnemyPerception()
		|| Receiver->GetCombatState() == EEnemyCombatState::Stun)
	{
		return false;
	}

	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Receiver);
	FEnemyRoomTargetContactState* Contact = TargetContactStates.Find(RoomTag);
	if (!IsRoomInCombat(RoomTag) || !Contact)
	{
		return false;
	}
	CompactTargetContactState(RoomTag, *Contact);
	const FEnemyRoomTargetObservation* Observation = Contact->TargetObservations.Find(TargetActor);
	if (!Observation)
	{
		return false;
	}

	bool bObserved = false;
	for (const auto& Observer : Contact->DirectObservers)
	{
		if (Observer.Value.TargetActor.Get() == TargetActor) { bObserved = true; break; }
	}
	const float ConfiguredGrace = Receiver->GetRuntimeStat().TargetTrackingGraceSeconds;
	const double Grace = FMath::IsFinite(ConfiguredGrace) ? FMath::Max(0.0f, ConfiguredGrace) : 0.0f;
	const double ExpiresAt = Observation->LastObservedAt + Grace;
	// 관측자 제거/조회 시각이 아니라 마지막 실제 관측 시각에 고정한다.
	// 관측 중에는 유효하며, 마지막 관측자 이탈 뒤에는 만료 경계를 포함해 차단한다.
	if (!bObserved && World->GetTimeSeconds() >= ExpiresAt)
	{
		return false;
	}

	OutSnapshot.TargetActor = TargetActor;
	OutSnapshot.LastObservedLocation = Observation->LastObservedLocation;
	OutSnapshot.LastObservedAt = Observation->LastObservedAt;
	OutSnapshot.TrackingExpiresAt = ExpiresAt;
	OutSnapshot.bDirectlyObserved = bObserved;
	return true;
}

bool UEnemyRoomSubsystem::TrySelectRoomTarget(
	AEnemyBase* Receiver, FEnemyRoomTargetObservationSnapshot& OutSnapshot, bool bRequireLiveObservation)
{
	OutSnapshot = {};
	if (!IsValid(Receiver))
	{
		return false;
	}
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Receiver->GetController());
	FEnemyRoomTargetContactState* Contact = TargetContactStates.Find(ResolveEnemyRoomTag(Receiver));
	if (!Controller || !Contact)
	{
		return false;
	}
	float BestDistanceSquared = TNumericLimits<float>::Max();
	auto BestPriority = OutlierEnemyTargetRules::ETargetPriority::Invalid;
	// 월드의 적을 검색하지 않고 이 Room에 직접 보고된 소수의 대상만 비교한다.
	// 조회 중 무효 기록이 제거될 수 있으므로 키만 복사한다. 일반적인 플레이어 수는 스택에 담는다.
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<8>> Candidates;
	for (const auto& Entry : Contact->TargetObservations)
	{
		Candidates.Add(Entry.Key);
	}
	for (const TWeakObjectPtr<AActor>& CandidateTarget : Candidates)
	{
		AActor* Target = CandidateTarget.Get();
		if (!Controller->IsValidTrackingTarget(Target))
		{
			continue;
		}
		FEnemyRoomTargetObservationSnapshot Candidate;
		if (!TryGetRoomTargetObservation(Receiver, Target, Candidate))
		{
			continue;
		}
		if (bRequireLiveObservation && !Candidate.bDirectlyObserved)
		{
			continue;
		}
		const auto Priority = OutlierEnemyTargetRules::GetPriority(Target);
		const float DistanceSquared = FVector::DistSquared(Receiver->GetActorLocation(), Target->GetActorLocation());
		if (Priority != OutlierEnemyTargetRules::ETargetPriority::Invalid
			&& (!OutSnapshot.TargetActor.IsValid() || Priority < BestPriority
				|| (Priority == BestPriority && DistanceSquared < BestDistanceSquared)))
		{
			OutSnapshot = Candidate;
			BestPriority = Priority;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return OutSnapshot.TargetActor.IsValid();
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
	if (!IsValid(Enemy))
	{
		return true;
	}
	const FGameplayTag RoomTag = Enemy->GetDefaultRoomTag();
	for (const auto& Entry : SearchDestinations)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		if (!Other || Other == Enemy || Other->IsDead() || !Other->GetSearchEpisode().bActive
			|| Other->GetSearchEpisode().bLocalGuard
			|| Other->GetDefaultRoomTag() != RoomTag)
		{
			continue;
		}
		for (const FVector& Point : Entry.Value)
		{
			if (FVector::DistSquared(Location, Point) < FMath::Square(Separation))
			{
				return true;
			}
		}
	}
	return false;
}

bool UEnemyRoomSubsystem::ReserveSearchDestinations(
	AEnemyBase* Enemy, const TArray<FVector>& Locations, float Separation)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->GetSearchEpisode().bActive
		|| Enemy->GetSearchEpisode().bLocalGuard
		|| Locations.Num() > Enemy->GetSearchPointLimit() || !FMath::IsFinite(Separation) || Separation <= 0.0f)
	{
		return false;
	}
	// 결과 적용 시 다시 검사한다. 먼저 끝난 질의가 같은 위치를 예약했을 수 있다.
	for (const FVector& Point : Locations)
	{
		if (Point.ContainsNaN() || IsSearchDestinationReserved(Enemy, Point, Separation))
		{
			return false;
		}
	}
	for (auto It = SearchDestinations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	SearchDestinations.Add(TWeakObjectPtr<AEnemyBase>(Enemy), Locations);
	return true;
}

void UEnemyRoomSubsystem::ReleaseSearchDestinations(AEnemyBase* Enemy)
{
	if (Enemy)
	{
		SearchDestinations.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
	}
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
	if (!IsValid(Enemy))
	{
		return false;
	}
	for (const auto& Entry : PatrolDestinations)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		if (Other == Enemy || !IsCurrentPatrolReservation(Other, Entry.Value)
			|| Entry.Value.RoomTag != Enemy->GetDefaultRoomTag())
		{
			continue;
		}
		if (FVector::DistSquared(Location, Entry.Value.Location) < FMath::Square(Separation))
		{
			return true;
		}
	}
	return false;
}

bool UEnemyRoomSubsystem::ReservePatrolDestination(AEnemyBase* Enemy, const FVector& Location)
{
	if (!IsValid(Enemy) || Location.ContainsNaN())
	{
		return false;
	}
	FEnemyPatrolDestination Reservation;
	Reservation.Location = Location;
	Reservation.RoomTag = Enemy->GetDefaultRoomTag();
	Reservation.Controller = Enemy->GetController();
	Reservation.Generation = Enemy->GetPoolGameplayGeneration();
	Reservation.Lease = Enemy->GetPoolLeaseSerial();
	if (!IsCurrentPatrolReservation(Enemy, Reservation))
	{
		return false;
	}
	for (auto It = PatrolDestinations.CreateIterator(); It; ++It)
	{
		if (!IsCurrentPatrolReservation(It.Key().Get(), It.Value()))
		{
			It.RemoveCurrent();
		}
	}
	// 순찰은 공유 목적지를 금지하지 않는다. 대체 지점이 없으면 같은 지점도 사용할 수 있다.
	PatrolDestinations.Add(TWeakObjectPtr<AEnemyBase>(Enemy), Reservation);
	return true;
}

void UEnemyRoomSubsystem::ReleasePatrolDestination(AEnemyBase* Enemy)
{
	if (Enemy)
	{
		PatrolDestinations.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
	}
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

namespace
{
constexpr double CombatReservationLifetime = 1.0;

double CombatSetting(float Value, double Fallback)
{
	return FMath::IsFinite(Value) && Value > 0.0f ? double(Value) : Fallback;
}

void LogCombatReservation(const TCHAR* Phase, const AEnemyBase* Enemy, uint64 Request, const AEnemyBase* Other = nullptr)
{
	static const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.EQSDebug"));
	if (!Debug || Debug->GetInt() == 0)
	{
		return;
	}
	UE_LOG(LogEnemySearchSharing, Log, TEXT("[CombatReservation] %s Enemy=%s Request=%llu Episode=%llu Other=%s"),
		Phase, *GetNameSafe(Enemy), Request, Enemy ? Enemy->GetCombatTacticalEpisodeSerial() : 0, *GetNameSafe(Other));
}

FBox CombatCorridorBounds(const FEnemyCombatCorridor& Corridor, const FVector& Extent)
{
	FBox Bounds(Corridor.Start, Corridor.Start);
	Bounds += Corridor.End;
	return Bounds.ExpandBy(Extent);
}

bool CombatReservationsOverlap(const FEnemyCombatReservation& A, const FEnemyCombatReservation& B)
{
	const FBox DestinationA(A.Destination - A.Extent, A.Destination + A.Extent);
	const FBox DestinationB(B.Destination - B.Extent, B.Destination + B.Extent);
	if (DestinationA.Intersect(DestinationB))
	{
		return true;
	}
	for (const FEnemyCombatCorridor& CorridorA : A.Corridors)
	{
		const FBox BoundsA = CombatCorridorBounds(CorridorA, A.Extent);
		if (BoundsA.Intersect(DestinationB))
		{
			return true;
		}
		for (const FEnemyCombatCorridor& CorridorB : B.Corridors)
		{
			if (BoundsA.Intersect(CombatCorridorBounds(CorridorB, B.Extent)))
			{
				return true;
			}
		}
	}
	for (const FEnemyCombatCorridor& CorridorB : B.Corridors)
	{
		if (DestinationA.Intersect(CombatCorridorBounds(CorridorB, B.Extent)))
		{
			return true;
		}
	}
	return false;
}

bool HasOccupiedCorridorPriority(const AEnemyBase& Enemy, const FEnemyCombatReservation& Demand,
	const AEnemyBase* Other, const FEnemyCombatReservation& Existing)
{
	if (!Other || FBox(Demand.Destination - Demand.Extent, Demand.Destination + Demand.Extent)
		.Intersect(FBox(Existing.Destination - Existing.Extent, Existing.Destination + Existing.Extent)))
	{
		return false;
	}
	const FVector Position = Enemy.GetCapsuleComponent()->GetComponentLocation();
	const FVector OtherPosition = Other->GetCapsuleComponent()->GetComponentLocation();
	for (const FEnemyCombatCorridor& A : Demand.Corridors)
	{
		const FBox BoundsA = CombatCorridorBounds(A, Demand.Extent);
		if (!BoundsA.IsInsideOrOn(Position))
		{
			continue;
		}
		for (const FEnemyCombatCorridor& B : Existing.Corridors)
		{
			const FBox BoundsB = CombatCorridorBounds(B, Existing.Extent);
			if (BoundsB.IsInsideOrOn(Position) && !BoundsA.IsInsideOrOn(OtherPosition) && !BoundsB.IsInsideOrOn(OtherPosition))
			{
				return true;
			}
		}
	}
	return false;
}
}

bool UEnemyRoomSubsystem::IsCombatReservationOwnerCurrent(const AEnemyBase* Enemy,
	const FEnemyCombatReservation& Reservation) const
{
	return IsValid(Enemy) && Enemy->HasAuthority() && !Enemy->IsDead() && Enemy->CanUseEnemyPerception()
		&& Enemy->GetRuntimeStat().Type == EEnemyType::Gun && Reservation.Request != 0
		&& Reservation.Controller.IsValid() && Enemy->GetController() == Reservation.Controller.Get()
		&& Reservation.Controller->GetPawn() == Enemy
		&& Enemy->GetPoolGameplayGeneration() == Reservation.Generation && Enemy->GetPoolLeaseSerial() == Reservation.Lease
		&& Enemy->IsCombatTacticalEpisodeCurrent(Reservation.Episode);
}

void UEnemyRoomSubsystem::CompactCombatReservations()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = CombatDestinations.CreateIterator(); It; ++It)
	{
		if (!IsCombatReservationOwnerCurrent(It.Key().Get(), It.Value()) || Now >= It.Value().ExpiresAt)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = CombatReservationRequests.CreateIterator(); It; ++It)
	{
		if (!IsCombatReservationOwnerCurrent(It.Key().Get(), It.Value().Demand))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = CombatYieldRequests.CreateIterator(); It; ++It)
	{
		if (!IsCombatYieldCurrent(It.Key().Get(), It.Value()))
		{
			It.RemoveCurrent();
		}
	}
}

void UEnemyRoomSubsystem::RefreshCombatReadiness(AEnemyBase* Enemy, FEnemyCombatReservationRequest& Request)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= Request.NextReadinessCheckAt)
	{
		Request.NextReadinessCheckAt = Now + (Enemy ? CombatSetting(Enemy->GetRuntimeStat().CombatYieldCheckInterval, 0.5) : 0.5);
		Request.bAttackReady = Enemy && Enemy->IsReadyForCombatYieldPriority();
	}
}

bool UEnemyRoomSubsystem::HasCombatContenderPriority(const AEnemyBase* AOwner, const FEnemyCombatReservationRequest& A,
	const AEnemyBase* BOwner, const FEnemyCombatReservationRequest& B) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double AStarvation = AOwner ? CombatSetting(AOwner->GetRuntimeStat().CombatYieldStarvationSeconds, 2.0) : 2.0;
	const double BStarvation = BOwner ? CombatSetting(BOwner->GetRuntimeStat().CombatYieldStarvationSeconds, 2.0) : 2.0;
	const bool bAStarved = A.WaitingSince >= 0.0 && Now - A.WaitingSince >= AStarvation;
	const bool bBStarved = B.WaitingSince >= 0.0 && Now - B.WaitingSince >= BStarvation;
	// 오래 막힌 개체의 다음 차례는 공격 가능한 개체의 짧은 우선권보다 앞선다.
	// 이는 대기 중 후보의 순서일 뿐, 이미 실행 중인 예약을 강탈하지 않는다.
	if (bAStarved != bBStarved)
	{
		return bAStarved;
	}
	if (bAStarved && bBStarved && A.WaitingSince != B.WaitingSince)
	{
		return A.WaitingSince < B.WaitingSince;
	}
	const bool bAHeld = Now < A.PriorityUntil;
	const bool bBHeld = Now < B.PriorityUntil;
	if (bAHeld != bBHeld)
	{
		return bAHeld;
	}
	if (A.bAttackReady != B.bAttackReady)
	{
		return A.bAttackReady;
	}
	return A.Ticket < B.Ticket;
}

uint64 UEnemyRoomSubsystem::BeginCombatReservationRequest(AEnemyBase* Enemy, uint64 Episode)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !Enemy->IsCombatTacticalEpisodeCurrent(Episode))
	{
		return 0;
	}
	CompactCombatReservations();
	FEnemyCombatReservationRequest& Pending = CombatReservationRequests.FindOrAdd(Enemy);
	if (Pending.Demand.Episode != Episode)
	{
		Pending = {};
		Pending.Ticket = ++NextCombatReservationTicket;
	}
	Pending.Demand.Controller = Enemy->GetController();
	Pending.Demand.Generation = Enemy->GetPoolGameplayGeneration();
	Pending.Demand.Lease = Enemy->GetPoolLeaseSerial();
	Pending.Demand.Episode = Episode;
	Pending.Demand.Request = ++NextCombatReservationRequest;
	Pending.bWaiting = false;
	Pending.bActive = true;
	return Pending.Demand.Request;
}

bool UEnemyRoomSubsystem::IsCombatReservationRequestCurrent(const AEnemyBase* Enemy, uint64 Request) const
{
	const FEnemyCombatReservationRequest* Pending = CombatReservationRequests.Find(Enemy);
	return Pending && Pending->bActive && Request != 0 && Pending->Demand.Request == Request
		&& IsCombatReservationOwnerCurrent(Enemy, Pending->Demand);
}

bool UEnemyRoomSubsystem::TryReserveCombatDestination(AEnemyBase* Enemy, uint64 Request,
	const FVector& Destination, const TArray<FEnemyCombatCorridor>& Corridors, const TArray<FVector>& Route, float Clearance)
{
	if (!IsCombatReservationRequestCurrent(Enemy, Request) || Destination.ContainsNaN()
		|| !FMath::IsFinite(Clearance) || Clearance < 0.0f || Corridors.Num() > 128)
	{
		return false;
	}
	for (const FEnemyCombatCorridor& Corridor : Corridors)
	{
		if (Corridor.Start.ContainsNaN() || Corridor.End.ContainsNaN())
		{
			return false;
		}
	}
	if (Route.Num() < 2 || Route.Num() > 1024)
	{
		return false;
	}
	for (const FVector& Point : Route)
	{
		if (Point.ContainsNaN())
		{
			return false;
		}
	}
	CompactCombatReservations();
	FEnemyCombatReservationRequest& Pending = CombatReservationRequests.FindChecked(Enemy);
	UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	if (!Capsule || !Capsule->IsQueryCollisionEnabled())
	{
		return false;
	}
	FEnemyCombatReservation& Demand = Pending.Demand;
	Demand.Destination = Destination + Capsule->GetComponentLocation() - Enemy->GetActorLocation();
	Demand.Corridors = Corridors;
	Demand.Route = Route;
	Demand.Extent = FlightPathfinding::GetCapsuleWorldExtent(Capsule->GetScaledCapsuleRadius() + Clearance,
		Capsule->GetScaledCapsuleHalfHeight() + Clearance, Capsule->GetComponentQuat());
	Demand.ProgressLocation = Capsule->GetComponentLocation();
	Demand.ObservationCenter = Enemy->GetCombatNavigationTargetLocation(Enemy->GetTrackedTarget());
	Demand.FurthestRouteProgress = 0.0;
	Demand.ExpiresAt = GetWorld()->GetTimeSeconds() + CombatReservationLifetime;
	Pending.bWaiting = true;
	if (Pending.WaitingSince < 0.0)
	{
		Pending.WaitingSince = GetWorld()->GetTimeSeconds();
	}
	RefreshCombatReadiness(Enemy, Pending);
	auto RequestYieldForRoute = [&](AEnemyBase* Other)
	{
		if (!IsValid(Other) || !Other->GetCapsuleComponent())
		{
			return;
		}
		const FVector Position = Other->GetCapsuleComponent()->GetComponentLocation();
		int32 ClosestSegment = 1;
		double ClosestDistance = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Demand.Route.Num(); ++Index)
		{
			const double Distance = FMath::PointDistToSegmentSquared(Position, Demand.Route[Index - 1], Demand.Route[Index]);
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ClosestSegment = Index;
			}
		}
		RequestCombatYield(Enemy, Other, Demand.Route[ClosestSegment - 1], Demand.Route[ClosestSegment]);
	};

	// 먼저 확보한 이동은 선점하지 않는다. 실패한 요청은 기존 예약을 버리지 않은 채 차순위 후보를 검사한다.
	// 단, 이미 통로 안에 있는 개체의 이탈은 밖에서 잡은 미래 허가보다 우선한다.
	TArray<TWeakObjectPtr<AEnemyBase>, TInlineAllocator<4>> Superseded;
	for (const auto& Entry : CombatDestinations)
	{
		if (Entry.Key.Get() != Enemy && CombatReservationsOverlap(Demand, Entry.Value))
		{
			if (HasOccupiedCorridorPriority(*Enemy, Demand, Entry.Key.Get(), Entry.Value))
			{
				Superseded.Add(Entry.Key);
				continue;
			}
			LogCombatReservation(TEXT("ReservedByOther"), Enemy, Request, Entry.Key.Get());
			RequestYieldForRoute(Entry.Key.Get());
			return false;
		}
	}
	for (auto& Entry : CombatReservationRequests)
	{
		if (Entry.Key.Get() != Enemy && Entry.Value.bActive && Entry.Value.bWaiting
			&& Entry.Value.Demand.ExpiresAt > GetWorld()->GetTimeSeconds()
			&& CombatReservationsOverlap(Demand, Entry.Value.Demand))
		{
			if (HasOccupiedCorridorPriority(*Enemy, Demand, Entry.Key.Get(), Entry.Value.Demand))
			{
				continue;
			}
			RefreshCombatReadiness(Entry.Key.Get(), Entry.Value);
			if (!HasCombatContenderPriority(Enemy, Pending, Entry.Key.Get(), Entry.Value))
			{
				if (GetWorld()->GetTimeSeconds() >= Entry.Value.PriorityUntil)
				{
					Entry.Value.PriorityUntil = GetWorld()->GetTimeSeconds()
						+ CombatSetting(Entry.Key->GetRuntimeStat().CombatYieldPriorityHold, 1.0);
				}
				LogCombatReservation(TEXT("PriorityContender"), Enemy, Request, Entry.Key.Get());
				return false;
			}
		}
	}

	// 홈 RoomTag가 달라도 같은 연결 공간의 실제 점유를 반영한다. 예약 반환/TTL은 캡슐을 비우지 않는다.
	// 등록 집합만 사용하며 월드 Actor 검색이나 추가 물리 Trace는 하지 않는다.
	for (const auto& Entry : RegisteredEnemyKeys)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		const UCapsuleComponent* OtherCapsule = IsValid(Other) ? Other->GetCapsuleComponent() : nullptr;
		if (Other == Enemy || !OtherCapsule || !OtherCapsule->IsQueryCollisionEnabled())
		{
			continue;
		}
		FEnemyCombatReservation Occupancy;
		Occupancy.Destination = OtherCapsule->GetComponentLocation();
		Occupancy.Extent = FlightPathfinding::GetCapsuleWorldExtent(OtherCapsule->GetScaledCapsuleRadius(),
			OtherCapsule->GetScaledCapsuleHalfHeight(), OtherCapsule->GetComponentQuat());
		if (CombatReservationsOverlap(Demand, Occupancy))
		{
			LogCombatReservation(TEXT("ActuallyOccupied"), Enemy, Request, Other);
			RequestYieldForRoute(const_cast<AEnemyBase*>(Other));
			return false;
		}
	}
	// 모든 목적지/대기/실제 점유 검사를 통과한 뒤에만 교체한다. 중간 실패가 다른 이동의 허가를 빼앗지 않는다.
	for (const TWeakObjectPtr<AEnemyBase>& PreviousOwner : Superseded)
	{
		LogCombatReservation(TEXT("YieldToOccupant"), PreviousOwner.Get(), CombatDestinations.FindChecked(PreviousOwner).Request, Enemy);
		CombatDestinations.Remove(PreviousOwner);
	}
	CombatDestinations.Add(Enemy, Demand);
	Pending.bWaiting = false;
	Pending.WaitingSince = -1.0;
	Pending.PriorityUntil = 0.0;
	// 한 번 통과한 개체는 다음 경쟁에서 기존 대기자의 뒤로 간다. 실행 중 예약 자체는 그대로 보호한다.
	Pending.Ticket = ++NextCombatReservationTicket;
	LogCombatReservation(TEXT("Acquired"), Enemy, Request);
	return true;
}

bool UEnemyRoomSubsystem::IsCombatReservationCurrent(const AEnemyBase* Enemy, uint64 Request) const
{
	const FEnemyCombatReservation* Reservation = CombatDestinations.Find(Enemy);
	return Reservation && Request != 0 && Reservation->Request == Request
		&& IsCombatReservationOwnerCurrent(Enemy, *Reservation) && GetWorld()->GetTimeSeconds() < Reservation->ExpiresAt;
}

bool UEnemyRoomSubsystem::RefreshCombatReservation(AEnemyBase* Enemy, uint64 Request)
{
	if (!IsCombatReservationCurrent(Enemy, Request))
	{
		return false;
	}
	FEnemyCombatReservation& Reservation = CombatDestinations.FindChecked(Enemy);
	const FVector Position = Enemy->GetCapsuleComponent()->GetComponentLocation();
	// 제자리 대기나 Task 재진입은 TTL을 늘리지 않는다. 진행하지 않는 미래 예약은 유한하게 끝난다.
	if (!Position.Equals(Reservation.ProgressLocation, 1.0f))
	{
		const FVector PreviousPosition = Reservation.ProgressLocation;
		Reservation.ProgressLocation = Position;
		double RouteDistance = 0.0;
		double ClosestDistance = TNumericLimits<double>::Max();
		double ForwardProgress = 0.0;
		for (int32 Index = 1; Index < Reservation.Route.Num(); ++Index)
		{
			const FVector Closest = FMath::ClosestPointOnSegment(Position, Reservation.Route[Index - 1], Reservation.Route[Index]);
			const double Distance = FVector::DistSquared(Position, Closest);
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ForwardProgress = RouteDistance + FVector::Distance(Reservation.Route[Index - 1], Closest);
			}
			RouteDistance += FVector::Distance(Reservation.Route[Index - 1], Reservation.Route[Index]);
		}
		if (ForwardProgress > Reservation.FurthestRouteProgress + 1.0 && ClosestDistance <= FMath::Square(25.0))
		{
			Reservation.FurthestRouteProgress = ForwardProgress;
			Reservation.ExpiresAt = GetWorld()->GetTimeSeconds() + CombatReservationLifetime;
		}
		Reservation.Corridors.RemoveAll([&](FEnemyCombatCorridor& Corridor)
		{
			const FBox Bounds = CombatCorridorBounds(Corridor, Reservation.Extent);
			Corridor.bEntered |= Bounds.IsInsideOrOn(Position) || Bounds.IsInsideOrOn(PreviousPosition)
				|| FMath::LineBoxIntersection(Bounds, PreviousPosition, Position, Position - PreviousPosition);
			const FVector Direction = (Corridor.End - Corridor.Start).GetSafeNormal();
			// 굽은 경로의 먼 구간을 현재 위치의 투영만으로 반환하지 않는다. 실제 진입 후 몸 전체가 빠져나와야 한다.
			return Corridor.bEntered && FVector::DotProduct(Position - Corridor.End, Direction) > Reservation.Extent.Size();
		});
	}
	return true;
}

bool UEnemyRoomSubsystem::IsCombatSegmentReserved(const AEnemyBase* Enemy, uint64 Request,
	const FVector& Start, const FVector& End) const
{
	if (!IsCombatReservationCurrent(Enemy, Request))
	{
		return false;
	}
	const FEnemyCombatReservation& Reservation = CombatDestinations.FindChecked(Enemy);
	// 이동 중 재탐색이 다른 좁은 길로 바뀌면 기존 허가를 전용하지 않는다. 새 Select에서 다시 확보한다.
	double RouteDistance = 0.0;
	for (int32 Index = 1; Index < Reservation.Route.Num(); ++Index)
	{
		if (FMath::PointDistToSegment(Start, Reservation.Route[Index - 1], Reservation.Route[Index]) <= 25.0f
			&& FMath::PointDistToSegment(End, Reservation.Route[Index - 1], Reservation.Route[Index]) <= 25.0f)
		{
			const FVector ClosestEnd = FMath::ClosestPointOnSegment(End, Reservation.Route[Index - 1], Reservation.Route[Index]);
			// 통과해 반환한 통로로 역주행할 때도 새 허가가 필요하다. 옛 전체 Route를 재진입 허가로 쓰지 않는다.
			return RouteDistance + FVector::Distance(Reservation.Route[Index - 1], ClosestEnd) + 25.0
				>= Reservation.FurthestRouteProgress;
		}
		RouteDistance += FVector::Distance(Reservation.Route[Index - 1], Reservation.Route[Index]);
	}
	return false;
}

void UEnemyRoomSubsystem::ReleaseCombatReservation(AEnemyBase* Enemy, uint64 Request)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority())
	{
		return;
	}
	if (Request == 0)
	{
		CombatYieldRequests.Remove(Enemy);
		for (auto It = CombatYieldRequests.CreateIterator(); It; ++It)
		{
			if (It.Value().Context.Requester.Get() == Enemy)
			{
				It.RemoveCurrent();
			}
		}
	}
	const FEnemyCombatReservation* Reservation = CombatDestinations.Find(Enemy);
	if (Reservation && (Request == 0 || Reservation->Request == Request))
	{
		LogCombatReservation(TEXT("Released"), Enemy, Reservation->Request);
		CombatDestinations.Remove(Enemy);
	}
	FEnemyCombatReservationRequest* Pending = CombatReservationRequests.Find(Enemy);
	if (!Pending)
	{
		return;
	}
	if (Request == 0)
	{
		CombatReservationRequests.Remove(Enemy);
	}
	else if (Pending->Demand.Request == Request)
	{
		Pending->bWaiting = false;
		Pending->bActive = false;
	}
}

bool UEnemyRoomSubsystem::IsCombatYieldCurrent(const AEnemyBase* Blocker, const FEnemyCombatYieldRequest& Request) const
{
	const AEnemyBase* Requester = Request.Context.Requester.Get();
	if (!GetWorld() || GetWorld()->GetTimeSeconds() >= Request.ExpiresAt || !IsValid(Blocker) || !IsValid(Requester)
		|| Blocker->GetWorld() != GetWorld() || Requester->GetWorld() != GetWorld()
		|| !Blocker->HasAuthority() || !Requester->HasAuthority() || Blocker->IsDead() || Requester->IsDead()
		|| !Blocker->CanUseEnemyPerception() || !Requester->CanUseEnemyPerception()
		|| Blocker->IsAIControlSuppressed() || Requester->IsAIControlSuppressed()
		|| Blocker->IsEnemyPossessed() || Requester->IsEnemyPossessed()
		|| Blocker->GetCombatState() != EEnemyCombatState::Combat || Requester->GetCombatState() != EEnemyCombatState::Combat
		|| !Blocker->IsA<AVECDrone>() || Blocker->GetRuntimeStat().Type != EEnemyType::Gun
		|| Cast<ALaserWeapon>(Blocker->GetCurrentWeapon())
		|| Blocker->GetGenericTeamId() != Requester->GetGenericTeamId()
		|| Blocker->GetController() != Request.BlockerController.Get()
		|| Requester->GetController() != Request.RequesterController.Get()
		|| !Request.BlockerController.IsValid() || Request.BlockerController->GetPawn() != Blocker
		|| !Request.RequesterController.IsValid() || Request.RequesterController->GetPawn() != Requester
		|| Blocker->GetPoolGameplayGeneration() != Request.BlockerGeneration || Blocker->GetPoolLeaseSerial() != Request.BlockerLease
		|| Requester->GetPoolGameplayGeneration() != Request.RequesterGeneration || Requester->GetPoolLeaseSerial() != Request.RequesterLease
		|| !Request.RequesterTarget.IsValid() || Requester->GetTrackedTarget() != Request.RequesterTarget.Get()
		|| !Request.BlockerTarget.IsValid() || Blocker->GetTrackedTarget() != Request.BlockerTarget.Get())
	{
		return false;
	}
	return Request.RequesterEpisode == 0 || Requester->IsCombatTacticalEpisodeCurrent(Request.RequesterEpisode);
}

bool UEnemyRoomSubsystem::RequestCombatYield(AEnemyBase* Requester, AEnemyBase* Blocker,
	const FVector& Start, const FVector& End)
{
	if (!GetWorld() || !IsValid(Requester) || !IsValid(Blocker) || Requester == Blocker
		|| Start.ContainsNaN() || End.ContainsNaN() || Start.Equals(End, 1.0))
	{
		return false;
	}
	FEnemyCombatYieldRequest Proposal;
	Proposal.Context.Requester = Requester;
	Proposal.Context.Start = Start;
	Proposal.Context.End = End;
	Proposal.RequesterController = Requester->GetController();
	Proposal.BlockerController = Blocker->GetController();
	Proposal.RequesterGeneration = Requester->GetPoolGameplayGeneration();
	Proposal.RequesterLease = Requester->GetPoolLeaseSerial();
	Proposal.BlockerGeneration = Blocker->GetPoolGameplayGeneration();
	Proposal.BlockerLease = Blocker->GetPoolLeaseSerial();
	Proposal.RequesterTarget = Requester->GetTrackedTarget();
	Proposal.BlockerTarget = Blocker->GetTrackedTarget();
	Proposal.RequesterEpisode = Requester->GetCombatTacticalEpisodeSerial();
	Proposal.ExpiresAt = GetWorld()->GetTimeSeconds() + CombatReservationLifetime;
	if (!IsCombatYieldCurrent(Blocker, Proposal))
	{
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	FEnemyCombatYieldRequest* Existing = CombatYieldRequests.Find(Blocker);
	const bool bExistingCurrent = Existing && IsCombatYieldCurrent(Blocker, *Existing);
	if (bExistingCurrent && Existing->Context.Requester.Get() != Requester)
	{
		// 한 양보자가 여러 방향으로 동시에 비키지 않는다. 실제로 갱신 중인 첫 요구는 유한 수명 안에서 처리한다.
		return false;
	}
	const bool bSameConflict = bExistingCurrent && Existing->Context.Start.Equals(Start, 100.0)
		&& Existing->Context.End.Equals(End, 100.0);
	Proposal.WaitingSince = bSameConflict ? Existing->WaitingSince : Now;
	Proposal.BlockerPriorityUntil = bSameConflict ? Existing->BlockerPriorityUntil : 0.0;
	Proposal.RequesterPriorityUntil = bSameConflict ? Existing->RequesterPriorityUntil : 0.0;
	Proposal.Context.Serial = bSameConflict ? Existing->Context.Serial : ++NextCombatYieldSerial;
	if (bSameConflict)
	{
		// 총구/관측점의 작은 흔들림은 검사 중인 보호 구간이나 최초 대기 시각을 바꾸지 않는다.
		Proposal.Context = Existing->Context;
	}
	FEnemyCombatReservationRequest RequesterPriority;
	FEnemyCombatReservationRequest BlockerPriority;
	FEnemyCombatReservationRequest* RequesterPending = CombatReservationRequests.Find(Requester);
	FEnemyCombatReservationRequest* BlockerPending = CombatReservationRequests.Find(Blocker);
	// 사선 차단은 정상 공격 불가이므로 공격 가능 여부만으로 요청 자체를 거절하지 않는다.
	// 같은 실제 충돌에서 양쪽의 준비 상태, 장기 대기와 안정된 순번을 함께 비교한다.
	RequesterPriority.Ticket = uint64(Requester->GetUniqueID());
	BlockerPriority.Ticket = uint64(Blocker->GetUniqueID());
	RefreshCombatReadiness(Requester, RequesterPending ? *RequesterPending : RequesterPriority);
	RefreshCombatReadiness(Blocker, BlockerPending ? *BlockerPending : BlockerPriority);
	if (RequesterPending)
	{
		RequesterPriority = *RequesterPending;
	}
	if (BlockerPending)
	{
		BlockerPriority = *BlockerPending;
	}
	RequesterPriority.WaitingSince = Proposal.WaitingSince;
	RequesterPriority.PriorityUntil = FMath::Max(RequesterPriority.PriorityUntil, Proposal.RequesterPriorityUntil);
	BlockerPriority.PriorityUntil = FMath::Max(BlockerPriority.PriorityUntil, Proposal.BlockerPriorityUntil);
	Proposal.bApproved = HasCombatContenderPriority(Requester, RequesterPriority, Blocker, BlockerPriority);
	if (!Proposal.bApproved && !bSameConflict)
	{
		Proposal.BlockerPriorityUntil = Now + CombatSetting(Blocker->GetRuntimeStat().CombatYieldPriorityHold, 1.0);
	}
	if (Proposal.bApproved && (!bSameConflict || !Existing->bApproved))
	{
		Proposal.RequesterPriorityUntil = Now + CombatSetting(Requester->GetRuntimeStat().CombatYieldPriorityHold, 1.0);
	}
	// 거절도 현재 충돌의 대기 이력으로 보존한다. 정상 사격 가능한 앞 개체가 영구 독점하지 않도록
	// 반복된 실제 보고만 2초를 누적하며, 조회/Task 재진입은 대기나 만료 시각을 갱신하지 않는다.
	CombatYieldRequests.Add(Blocker, Proposal);
	return Proposal.bApproved;
}

bool UEnemyRoomSubsystem::GetCombatYieldContext(const AEnemyBase* Enemy, FEnemyCombatYieldContext& Out) const
{
	Out = {};
	const FEnemyCombatYieldRequest* Request = CombatYieldRequests.Find(Enemy);
	if (!Request || !Request->bApproved || !IsCombatYieldCurrent(Enemy, *Request))
	{
		return false;
	}
	Out = Request->Context;
	return true;
}

void UEnemyRoomSubsystem::FinishCombatYield(const AEnemyBase* Enemy, uint64 Serial)
{
	const FEnemyCombatYieldRequest* Request = CombatYieldRequests.Find(Enemy);
	if (Request && Serial != 0 && Request->Context.Serial == Serial)
	{
		CombatYieldRequests.Remove(Enemy);
	}
}

double UEnemyRoomSubsystem::GetCombatObservationCrowding(const AEnemyBase* Enemy,
	const FVector& Center, const FVector& Candidate) const
{
	if (!IsValid(Enemy) || !GetWorld() || Center.ContainsNaN() || Candidate.ContainsNaN())
	{
		return 0.0;
	}
	const FVector Direction = (Candidate - Center).GetSafeNormal2D();
	const double PreferredAngle = FMath::Min(180.0, CombatSetting(Enemy->GetRuntimeStat().CombatYieldObservationAngle, 30.0));
	double Crowding = 0.0;
	auto AddPosition = [&](const FVector& Position)
	{
		const FVector OtherDirection = (Position - Center).GetSafeNormal2D();
		if (Direction.IsNearlyZero() || OtherDirection.IsNearlyZero())
		{
			return;
		}
		const double Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, OtherDirection), -1.0, 1.0)));
		Crowding += FMath::Max(0.0, 1.0 - Degrees / PreferredAngle);
	};
	for (const auto& Entry : CombatDestinations)
	{
		if (Entry.Key.Get() != Enemy && IsCombatReservationOwnerCurrent(Entry.Key.Get(), Entry.Value)
			&& GetWorld()->GetTimeSeconds() < Entry.Value.ExpiresAt
			&& FVector::DistSquared(Center, Entry.Value.ObservationCenter) <= FMath::Square(200.0))
		{
			AddPosition(Entry.Value.Destination);
		}
	}
	for (const auto& Entry : SearchDestinations)
	{
		const AEnemyBase* Other = Entry.Key.Get();
		if (!IsValid(Other) || Other == Enemy || Other->IsDead() || !Other->CanUseEnemyPerception())
		{
			continue;
		}
		const FEnemySearchEpisode& Search = Other->GetSearchEpisode();
		if (!Search.bActive || Search.bLocalGuard || Search.Controller.Get() != Other->GetController()
			|| Search.Generation != Other->GetPoolGameplayGeneration() || Search.Lease != Other->GetPoolLeaseSerial()
			|| FVector::DistSquared(Center, Search.Center) > FMath::Square(200.0))
		{
			continue;
		}
		for (const FVector& Position : Entry.Value)
		{
			AddPosition(Position);
		}
	}
	return Crowding;
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
	CombatDestinations.Reset();
	CombatReservationRequests.Reset();
	CombatYieldRequests.Reset();
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
