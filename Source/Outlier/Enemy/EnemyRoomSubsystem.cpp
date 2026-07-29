#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

void UEnemyRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UOutlierArenaPoolSubsystem* ArenaPool = Collection.InitializeDependency<UOutlierArenaPoolSubsystem>();
	if (ArenaPool)
	{
		ArenaPool->OnArenaReleased.AddUObject(this, &UEnemyRoomSubsystem::HandleArenaReleased);
	}
}

void UEnemyRoomSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaPool->OnArenaReleased.RemoveAll(this);
		}
	}

	CombatRoomsByArena.Reset();
	RegisteredEnemiesByRoom.Reset();
	RegisteredEnemyKeys.Reset();
	SearchStates.Reset();
	TargetContactStates.Reset();
	Super::Deinitialize();
}

void UEnemyRoomSubsystem::RegisterEnemy(AEnemyBase* Enemy)
{
	if (IsValid(Enemy) && Enemy->HasAuthority())
	{
		RefreshEnemyRegistration(Enemy);
	}
}

void UEnemyRoomSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyKey(Enemy);
	if (const FEnemyRoomSearchKey* RegisteredKey = RegisteredEnemyKeys.Find(EnemyKey))
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
	const FEnemyRoomSearchKey NewKey = ResolveEnemyRegistrationKey(Enemy);
	if (!NewKey.RoomTag.IsValid())
	{
		UnregisterEnemy(Enemy);
		return;
	}

	if (const FEnemyRoomSearchKey* PreviousKey = RegisteredEnemyKeys.Find(EnemyPtr))
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

void UEnemyRoomSubsystem::NotifyRoomCombat(int32 ArenaId, FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || ArenaId == INDEX_NONE || !RoomTag.IsValid())
	{
		return;
	}

	UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>();
	if (!ArenaPool)
	{
		return;
	}

	ULevel* ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
	if (!ArenaLevel)
	{
		return;
	}

	TSet<FGameplayTag>& CombatRooms = CombatRoomsByArena.FindOrAdd(ArenaId);
	CombatRooms.Add(RoomTag);

	const FEnemyRoomSearchKey Key{ArenaId, RoomTag};
	CompactRegisteredEnemies(Key);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(Key);
	if (RegisteredEnemies)
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
		{
			AEnemyBase* Enemy = EnemyPtr.Get();
			if (!IsValid(Enemy) || Enemy == ExcludeEnemy)
			{
				continue;
			}

			Enemy->EnterCombatInArena(PlayerLocation, ArenaId, false);
		}
	}

	if (const FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(Key))
	{
		BroadcastSharedTargetContact(Key, ContactState->LastReportedLocation);
	}
}

bool UEnemyRoomSubsystem::IsRoomInCombat(int32 ArenaId, FGameplayTag RoomTag) const
{
	if (ArenaId == INDEX_NONE || !RoomTag.IsValid())
	{
		return false;
	}

	const TSet<FGameplayTag>* CombatRooms = CombatRoomsByArena.Find(ArenaId);
	return CombatRooms && CombatRooms->Contains(RoomTag);
}

void UEnemyRoomSubsystem::ReportRoomTargetContact(
	AEnemyBase* Observer,
	AActor* TargetActor,
	int32 ArenaId,
	const FVector& TargetLocation)
{
	UWorld* World = GetWorld();
	if (!World
		|| World->GetNetMode() == NM_Client
		|| !IsValid(Observer)
		|| !IsValid(TargetActor)
		|| !Observer->HasAuthority())
	{
		return;
	}

	const int32 ResolvedArenaId = ArenaId != INDEX_NONE
		? ArenaId
		: Observer->GetLastKnownArenaId();
	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Observer);
	if (ResolvedArenaId == INDEX_NONE || !RoomTag.IsValid())
	{
		return;
	}

	const FEnemyRoomSearchKey Key{ResolvedArenaId, RoomTag};
	FEnemyRoomTargetContactState& ContactState = TargetContactStates.FindOrAdd(Key);
	CompactTargetContactState(ContactState);
	ContactState.DirectObservers.Add(TWeakObjectPtr<AEnemyBase>(Observer));

	const bool bTargetChanged = ContactState.TargetActor.Get() != TargetActor;
	const bool bLocationChanged = FVector::DistSquared(
		ContactState.LastReportedLocation,
		TargetLocation) > FMath::Square(50.0f);

	ContactState.TargetActor = TargetActor;
	if (!bTargetChanged && !bLocationChanged)
	{
		return;
	}

	ContactState.LastReportedLocation = TargetLocation;
	BroadcastSharedTargetContact(Key, TargetLocation);
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
		ContactState.DirectObservers.Remove(ObserverKey);
		CompactTargetContactState(ContactState);
		if (!ContactState.DirectObservers.IsEmpty())
		{
			continue;
		}

		BroadcastSharedTargetLost(ContactIt.Key());
		ContactIt.RemoveCurrent();
	}
}

void UEnemyRoomSubsystem::NotifyTargetActorRemoved(AActor* TargetActor)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !TargetActor)
	{
		return;
	}

	TArray<FEnemyRoomSearchKey> RemovedContactKeys;
	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		if (ContactIt.Value().TargetActor.Get() == TargetActor
			|| !ContactIt.Value().TargetActor.IsValid())
		{
			RemovedContactKeys.Add(ContactIt.Key());
			ContactIt.RemoveCurrent();
		}
	}

	for (const FEnemyRoomSearchKey& Key : RemovedContactKeys)
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
	for (const TPair<FEnemyRoomSearchKey, TSet<TWeakObjectPtr<AEnemyBase>>>& RoomEntry :
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

bool UEnemyRoomSubsystem::RequestSearchRingSlot(
	AEnemyBase* Enemy,
	const FVector& Center,
	float Radius,
	float FlightHeightOffset,
	float ReassignmentDistance,
	float FloorTraceHalfHeight,
	FVector& OutSlotLocation)
{
	if (!IsValid(Enemy))
	{
		return false;
	}

	const FGameplayTag RoomTag = ResolveEnemyRoomTag(Enemy);
	if (!Enemy->HasAuthority()
		|| Enemy->GetLastKnownArenaId() == INDEX_NONE
		|| Enemy->GetRuntimeStat().MoveSpeed <= KINDA_SMALL_NUMBER
		|| Enemy->GetCombatState() != EEnemyCombatState::Combat
		|| Enemy->IsPlayerCurrentlyVisible()
		|| Enemy->HasSharedTargetContact()
		|| Enemy->IsEnemyPossessed()
		|| !RoomTag.IsValid())
	{
		return false;
	}

	const FEnemyRoomSearchKey Key{Enemy->GetLastKnownArenaId(), RoomTag};
	const TWeakObjectPtr<AEnemyBase> EnemyKey(Enemy);
	FEnemyRoomSearchState* SearchState = SearchStates.Find(Key);
	if (SearchState)
	{
		CompactSearchState(*SearchState);
	}

	// LKP가 충분히 움직였거나 슬롯 형태/참여 Enemy가 달라졌을 때만 방 전체 배치를 다시 만든다.
	const bool bNeedsRebuild = !SearchState
		|| FVector::DistSquared2D(SearchState->Center, Center)
			> FMath::Square(FMath::Max(ReassignmentDistance, 0.0f))
		|| !FMath::IsNearlyEqual(SearchState->Radius, Radius)
		|| !FMath::IsNearlyEqual(SearchState->FlightHeightOffset, FlightHeightOffset)
		|| !FMath::IsNearlyEqual(SearchState->FloorTraceHalfHeight, FloorTraceHalfHeight)
		|| !SearchState->Assignments.Contains(EnemyKey);

	if (bNeedsRebuild
		&& !RebuildSearchRingAssignments(
			Key,
			Center,
			Radius,
			FlightHeightOffset,
			FloorTraceHalfHeight))
	{
		return false;
	}

	SearchState = SearchStates.Find(Key);
	if (!SearchState)
	{
		return false;
	}

	const FVector* AssignedLocation = SearchState->Assignments.Find(EnemyKey);
	if (!AssignedLocation)
	{
		return false;
	}

	OutSlotLocation = *AssignedLocation;
	return true;
}

void UEnemyRoomSubsystem::ReleaseSearchRingSlot(AEnemyBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	for (auto SearchStateIt = SearchStates.CreateIterator(); SearchStateIt; ++SearchStateIt)
	{
		SearchStateIt.Value().Assignments.Remove(TWeakObjectPtr<AEnemyBase>(Enemy));
		CompactSearchState(SearchStateIt.Value());
		if (SearchStateIt.Value().Assignments.IsEmpty())
		{
			SearchStateIt.RemoveCurrent();
		}
	}
}

bool UEnemyRoomSubsystem::RebuildSearchRingAssignments(
	const FEnemyRoomSearchKey& Key,
	const FVector& Center,
	float Radius,
	float FlightHeightOffset,
	float FloorTraceHalfHeight)
{
	constexpr float MinimumEnemyFlightZ = 150.0f;
	constexpr int32 SearchRingPhaseCount = 8;

	UWorld* World = GetWorld();
	UOutlierArenaPoolSubsystem* ArenaPool = World
		? World->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaPool ? ArenaPool->GetArenaLoadedLevel(Key.ArenaId) : nullptr;
	if (!World || !ArenaLevel)
	{
		return false;
	}

	TArray<AEnemyBase*> EligibleEnemies;
	CompactRegisteredEnemies(Key);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(Key);
	if (!RegisteredEnemies)
	{
		SearchStates.Remove(Key);
		return false;
	}

	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
	{
		AEnemyBase* Candidate = EnemyPtr.Get();
		if (!IsValid(Candidate)
			|| Candidate->GetCombatState() != EEnemyCombatState::Combat
			|| Candidate->IsPlayerCurrentlyVisible()
			|| Candidate->HasSharedTargetContact()
			|| Candidate->IsEnemyPossessed()
			|| Candidate->GetRuntimeStat().MoveSpeed <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		EligibleEnemies.Add(Candidate);
	}

	if (EligibleEnemies.IsEmpty())
	{
		SearchStates.Remove(Key);
		return false;
	}

	EligibleEnemies.Sort(
		[](const AEnemyBase& Left, const AEnemyBase& Right)
		{
			return Left.GetFName().LexicalLess(Right.GetFName());
		});

	TArray<FVector> AvailableSlots;
	AvailableSlots.Reserve(EligibleEnemies.Num());
	const float SafeRadius = FMath::Max(Radius, 0.0f);
	const float TraceHalfHeight = FMath::Max(FloorTraceHalfHeight, 1.0f);

	// Keep the configured radius and equal spacing, but rotate the whole ring when
	// the first layout falls outside walkable floor geometry.
	for (int32 PhaseIndex = 0;
		PhaseIndex < SearchRingPhaseCount && AvailableSlots.IsEmpty();
		++PhaseIndex)
	{
		TArray<FVector> PhaseSlots;
		PhaseSlots.Reserve(EligibleEnemies.Num());
		const float PhaseOffset = UE_TWO_PI
			* static_cast<float>(PhaseIndex)
			/ static_cast<float>(SearchRingPhaseCount);

		for (int32 SlotIndex = 0; SlotIndex < EligibleEnemies.Num(); ++SlotIndex)
		{
			const float AngleRadians = PhaseOffset
				+ UE_TWO_PI
					* static_cast<float>(SlotIndex)
					/ static_cast<float>(EligibleEnemies.Num());
			const FVector2D Offset(
				FMath::Cos(AngleRadians) * SafeRadius,
				FMath::Sin(AngleRadians) * SafeRadius);
			const FVector TraceOrigin(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z);

			FHitResult FloorHit;
			const bool bFoundFloor = World->LineTraceSingleByChannel(
				FloorHit,
				TraceOrigin + FVector::UpVector * TraceHalfHeight,
				TraceOrigin - FVector::UpVector * TraceHalfHeight,
				ECC_WorldStatic);
			if (!bFoundFloor)
			{
				PhaseSlots.Reset();
				break;
			}

			PhaseSlots.Add(FVector(
				TraceOrigin.X,
				TraceOrigin.Y,
				FMath::Max(
					FloorHit.ImpactPoint.Z + FlightHeightOffset,
					MinimumEnemyFlightZ)));
		}

		if (PhaseSlots.Num() == EligibleEnemies.Num())
		{
			AvailableSlots = MoveTemp(PhaseSlots);
		}
	}

	if (AvailableSlots.Num() != EligibleEnemies.Num())
	{
		return false;
	}

	FEnemyRoomSearchState NewState;
	NewState.Center = Center;
	NewState.Radius = SafeRadius;
	NewState.FlightHeightOffset = FlightHeightOffset;
	NewState.FloorTraceHalfHeight = FloorTraceHalfHeight;

	// 이름 순서로 고정한 Enemy 각각에 현재 위치에서 가장 가까운 미사용 슬롯을 배정한다.
	// 같은 입력에서는 항상 같은 결과가 나와 멀티플레이 디버깅이 쉬워진다.
	for (AEnemyBase* Candidate : EligibleEnemies)
	{
		int32 BestSlotIndex = INDEX_NONE;
		float BestDistanceSquared = TNumericLimits<float>::Max();
		for (int32 SlotIndex = 0; SlotIndex < AvailableSlots.Num(); ++SlotIndex)
		{
			const float DistanceSquared = FVector::DistSquared(
				Candidate->GetActorLocation(),
				AvailableSlots[SlotIndex]);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestSlotIndex = SlotIndex;
			}
		}

		if (BestSlotIndex == INDEX_NONE)
		{
			return false;
		}

		NewState.Assignments.Add(
			TWeakObjectPtr<AEnemyBase>(Candidate),
			AvailableSlots[BestSlotIndex]);
		AvailableSlots.RemoveAtSwap(BestSlotIndex, EAllowShrinking::No);
	}

	SearchStates.Add(Key, MoveTemp(NewState));
	return true;
}

void UEnemyRoomSubsystem::CompactSearchState(FEnemyRoomSearchState& SearchState)
{
	for (auto AssignmentIt = SearchState.Assignments.CreateIterator(); AssignmentIt; ++AssignmentIt)
	{
		if (!AssignmentIt.Key().IsValid())
		{
			AssignmentIt.RemoveCurrent();
		}
	}
}

void UEnemyRoomSubsystem::BroadcastSharedTargetContact(
	const FEnemyRoomSearchKey& Key,
	const FVector& TargetLocation)
{
	UWorld* World = GetWorld();
	UOutlierArenaPoolSubsystem* ArenaPool = World
		? World->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaPool ? ArenaPool->GetArenaLoadedLevel(Key.ArenaId) : nullptr;
	if (!ArenaLevel)
	{
		return;
	}

	SearchStates.Remove(Key);

	CompactRegisteredEnemies(Key);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(Key);
	if (!RegisteredEnemies)
	{
		return;
	}

	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : *RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy)
			|| Enemy->GetCombatState() != EEnemyCombatState::Combat
			|| Enemy->IsEnemyPossessed())
		{
			continue;
		}

		Enemy->ApplySharedTargetContact(TargetLocation);
	}
}

void UEnemyRoomSubsystem::BroadcastSharedTargetLost(const FEnemyRoomSearchKey& Key)
{
	UWorld* World = GetWorld();
	UOutlierArenaPoolSubsystem* ArenaPool = World
		? World->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaPool ? ArenaPool->GetArenaLoadedLevel(Key.ArenaId) : nullptr;
	if (!ArenaLevel)
	{
		return;
	}

	CompactRegisteredEnemies(Key);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(Key);
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
		if (!(*ObserverIt).IsValid())
		{
			ObserverIt.RemoveCurrent();
		}
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

FEnemyRoomSearchKey UEnemyRoomSubsystem::ResolveEnemyRegistrationKey(
	const AEnemyBase* Enemy) const
{
	FEnemyRoomSearchKey Key;
	if (!Enemy)
	{
		return Key;
	}

	Key.ArenaId = Enemy->GetLastKnownArenaId();
	Key.RoomTag = ResolveEnemyRoomTag(Enemy);
	if (Key.ArenaId != INDEX_NONE)
	{
		return Key;
	}

	const ULevel* EnemyLevel = Enemy->GetLevel();
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Key;
	}

	if (EnemyLevel == World->PersistentLevel)
	{
		Key.ArenaId = 0;
		return Key;
	}

	const UOutlierArenaPoolSubsystem* ArenaPool =
		World->GetSubsystem<UOutlierArenaPoolSubsystem>();
	if (!ArenaPool)
	{
		return Key;
	}

	for (int32 ArenaId = 0; ArenaId < ArenaPool->MaxArenaCount; ++ArenaId)
	{
		if (ArenaPool->GetArenaLoadedLevel(ArenaId) == EnemyLevel)
		{
			Key.ArenaId = ArenaId;
			break;
		}
	}

	return Key;
}

void UEnemyRoomSubsystem::CompactRegisteredEnemies(const FEnemyRoomSearchKey& Key)
{
	TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(Key);
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
		RegisteredEnemiesByRoom.Remove(Key);
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

void UEnemyRoomSubsystem::HandleArenaReleased(int32 ArenaId)
{
	CombatRoomsByArena.Remove(ArenaId);

	for (auto RoomIt = RegisteredEnemiesByRoom.CreateIterator(); RoomIt; ++RoomIt)
	{
		if (RoomIt.Key().ArenaId != ArenaId)
		{
			continue;
		}

		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RoomIt.Value())
		{
			RegisteredEnemyKeys.Remove(EnemyPtr);
		}
		RoomIt.RemoveCurrent();
	}

	for (auto SearchStateIt = SearchStates.CreateIterator(); SearchStateIt; ++SearchStateIt)
	{
		if (SearchStateIt.Key().ArenaId == ArenaId)
		{
			SearchStateIt.RemoveCurrent();
		}
	}

	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		if (ContactIt.Key().ArenaId == ArenaId)
		{
			ContactIt.RemoveCurrent();
		}
	}
}
