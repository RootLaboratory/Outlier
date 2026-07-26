#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyBase.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyRoom, Log, All);

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
	RegisteredEnemies.Reset();
	SearchStates.Reset();
	TargetContactStates.Reset();
	Super::Deinitialize();
}

void UEnemyRoomSubsystem::RegisterEnemy(AEnemyBase* Enemy)
{
	if (IsValid(Enemy) && Enemy->HasAuthority())
	{
		RegisteredEnemies.Add(Enemy);
	}
}

void UEnemyRoomSubsystem::UnregisterEnemy(AEnemyBase* Enemy)
{
	RegisteredEnemies.Remove(Enemy);
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

	CompactRegisteredEnemies();

	int32 PropagatedEnemyCount = 0;
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy)
			|| Enemy == ExcludeEnemy
			|| Enemy->GetDefaultRoomTag() != RoomTag
			|| !IsEnemyInArena(Enemy, ArenaId, ArenaLevel))
		{
			continue;
		}

		Enemy->EnterCombatInArena(PlayerLocation, ArenaId, false);
		++PropagatedEnemyCount;
	}

	UE_LOG(
		LogEnemyRoom,
		Display,
		TEXT("[RoomCombat] ArenaId=%d RoomTag=%s Source=%s Registered=%d Propagated=%d"),
		ArenaId,
		*RoomTag.ToString(),
		*GetNameSafe(ExcludeEnemy),
		RegisteredEnemies.Num(),
		PropagatedEnemyCount);

	const FEnemyRoomSearchKey Key{ArenaId, RoomTag};
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
		UE_LOG(
			LogEnemyRoom,
			Warning,
			TEXT("[SearchRing][RequestRejected] Enemy=%s Authority=%d ArenaId=%d RoomTag=%s MoveSpeed=%.1f CombatState=%s Visible=%d SharedContact=%d Possessed=%d"),
			*GetNameSafe(Enemy),
			Enemy->HasAuthority() ? 1 : 0,
			Enemy->GetLastKnownArenaId(),
			*RoomTag.ToString(),
			Enemy->GetRuntimeStat().MoveSpeed,
			*UEnum::GetValueAsString(Enemy->GetCombatState()),
			Enemy->IsPlayerCurrentlyVisible() ? 1 : 0,
			Enemy->HasSharedTargetContact() ? 1 : 0,
			Enemy->IsEnemyPossessed() ? 1 : 0);
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
		UE_LOG(
			LogEnemyRoom,
			Warning,
			TEXT("[SearchRing][RebuildRejected] ArenaId=%d RoomTag=%s World=%d ArenaPool=%d ArenaLevel=%d"),
			Key.ArenaId,
			*Key.RoomTag.ToString(),
			World ? 1 : 0,
			ArenaPool ? 1 : 0,
			ArenaLevel ? 1 : 0);
		return false;
	}

	TArray<AEnemyBase*> EligibleEnemies;
	CompactRegisteredEnemies();
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RegisteredEnemies)
	{
		AEnemyBase* Candidate = EnemyPtr.Get();
		if (!IsValid(Candidate)
			|| !IsEnemyInArena(Candidate, Key.ArenaId, ArenaLevel)
			|| ResolveEnemyRoomTag(Candidate) != Key.RoomTag
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
		UE_LOG(
			LogEnemyRoom,
			Warning,
			TEXT("[SearchRing][NoEligibleEnemies] ArenaId=%d RoomTag=%s Registered=%d"),
			Key.ArenaId,
			*Key.RoomTag.ToString(),
			RegisteredEnemies.Num());
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
		UE_LOG(
			LogEnemyRoom,
			Warning,
			TEXT("[SearchRing] No floor-valid layout. ArenaId=%d RoomTag=%s Center=%s Radius=%.1f EnemyCount=%d TraceHalfHeight=%.1f"),
			Key.ArenaId,
			*Key.RoomTag.ToString(),
			*Center.ToCompactString(),
			SafeRadius,
			EligibleEnemies.Num(),
			TraceHalfHeight);
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

	CompactRegisteredEnemies();
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy)
			|| !IsEnemyInArena(Enemy, Key.ArenaId, ArenaLevel)
			|| ResolveEnemyRoomTag(Enemy) != Key.RoomTag
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

	CompactRegisteredEnemies();
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : RegisteredEnemies)
	{
		AEnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy)
			|| !IsEnemyInArena(Enemy, Key.ArenaId, ArenaLevel)
			|| ResolveEnemyRoomTag(Enemy) != Key.RoomTag)
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

bool UEnemyRoomSubsystem::IsEnemyInArena(
	const AEnemyBase* Enemy,
	int32 ArenaId,
	const ULevel* ArenaLevel) const
{
	if (!Enemy || !ArenaLevel)
	{
		return false;
	}

	const int32 EnemyArenaId = Enemy->GetLastKnownArenaId();
	if (EnemyArenaId != INDEX_NONE)
	{
		return EnemyArenaId == ArenaId;
	}

	const ULevel* EnemyLevel = Enemy->GetLevel();
	const UWorld* World = GetWorld();
	return EnemyLevel == ArenaLevel
		|| (World && EnemyLevel == World->PersistentLevel && ArenaId == 0);
}

void UEnemyRoomSubsystem::CompactRegisteredEnemies()
{
	for (auto EnemyIt = RegisteredEnemies.CreateIterator(); EnemyIt; ++EnemyIt)
	{
		if (!(*EnemyIt).IsValid())
		{
			EnemyIt.RemoveCurrent();
		}
	}
}

void UEnemyRoomSubsystem::HandleArenaReleased(int32 ArenaId)
{
	CombatRoomsByArena.Remove(ArenaId);

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
