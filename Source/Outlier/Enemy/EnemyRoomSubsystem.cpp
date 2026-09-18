#include "Enemy/EnemyRoomSubsystem.h"

#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyTargetRules.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomCombatSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"

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

void UEnemyRoomSubsystem::NotifyRoomCombat(FGameplayTag RoomTag, const FVector& PlayerLocation, AEnemyBase* ExcludeEnemy)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !RoomTag.IsValid())
	{
		return;
	}

	UOutlierArenaSubsystem* ArenaSubsystem = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!ArenaSubsystem)
	{
		return;
	}

	ULevel* ArenaLevel = ArenaSubsystem->GetArenaLoadedLevel();
	if (!ArenaLevel)
	{
		return;
	}

	if (URoomCombatSubsystem* CombatSubsystem =
		World->GetSubsystem<URoomCombatSubsystem>())
	{
		// 전투 정의가 있는 방은 새 관리자의 단일 활성 Room 판정을 통과한 경우에만 AI 전파한다.
		if (CombatSubsystem->IsRoomRegistered(RoomTag)
			&& !CombatSubsystem->NotifyRoomCombatStarted(RoomTag))
		{
			return;
		}
	}
	CombatRooms.Add(RoomTag);

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

			Enemy->EnterCombatFromRoom(PlayerLocation, false);
		}
	}

	if (const FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag))
	{
		BroadcastSharedTargetContact(RoomTag, ContactState->LastReportedLocation);
	}
}

void UEnemyRoomSubsystem::NotifyRoomCombatEnded(FGameplayTag RoomTag)
{
	// 여기서는 AI 공유 정보만 해제한다. Wave 완료/다음 차수 판정은 RoomCombatSubsystem이 소유한다.
	if (!RoomTag.IsValid())
	{
		return;
	}

	CombatRooms.Remove(RoomTag);
	SearchStates.Remove(RoomTag);
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
	World->GetTimerManager().ClearTimer(ContactState.ForcedShareTimerHandle);
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
		ContactState.DirectObservers.Remove(ObserverKey);
		CompactTargetContactState(ContactState);
		if (!ContactState.DirectObservers.IsEmpty())
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
	if (!World || !ContactState || !ContactState->TargetActor.IsValid())
	{
		return;
	}

	World->GetTimerManager().ClearTimer(ContactState->ForcedShareTimerHandle);
	World->GetTimerManager().SetTimer(
		ContactState->ForcedShareTimerHandle,
		FTimerDelegate::CreateUObject(this, &UEnemyRoomSubsystem::HandleForcedTargetShare, RoomTag),
		FMath::Max(ForcedTargetShareDelay, KINDA_SMALL_NUMBER),
		false);
}

void UEnemyRoomSubsystem::HandleForcedTargetShare(FGameplayTag RoomTag)
{
	FEnemyRoomTargetContactState* ContactState = TargetContactStates.Find(RoomTag);
	if (!ContactState)
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
		// 타겟 불가 상태 중 경과 시간을 인정하지 않고 해제 뒤 다시 8초를 계산한다.
		ScheduleForcedTargetShare(RoomTag);
		return;
	}

	ContactState->LastReportedLocation = TargetActor->GetActorLocation();
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
		// 고정형 터렛은 능력치가 바뀌어도 이동용 수색 Ring에 참여하지 않는다.
		|| Enemy->GetRuntimeStat().Type == EEnemyType::Turret
		|| Enemy->GetRuntimeStat().MoveSpeed <= KINDA_SMALL_NUMBER
		|| Enemy->GetCombatState() != EEnemyCombatState::Combat
		|| Enemy->IsPlayerCurrentlyVisible()
		|| Enemy->HasSharedTargetContact()
		|| Enemy->IsEnemyPossessed()
		|| !RoomTag.IsValid())
	{
		return false;
	}

	const FGameplayTag Key = RoomTag;
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
	FGameplayTag RoomTag,
	const FVector& Center,
	float Radius,
	float FlightHeightOffset,
	float FloorTraceHalfHeight)
{
	constexpr float MinimumEnemyFlightZ = 150.0f;
	constexpr int32 SearchRingPhaseCount = 8;

	UWorld* World = GetWorld();
	UOutlierArenaSubsystem* ArenaSubsystem = World
		? World->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaSubsystem ? ArenaSubsystem->GetArenaLoadedLevel() : nullptr;
	if (!World || !ArenaLevel)
	{
		return false;
	}

	TArray<AEnemyBase*> EligibleEnemies;
	CompactRegisteredEnemies(RoomTag);
	const TSet<TWeakObjectPtr<AEnemyBase>>* RegisteredEnemies =
		RegisteredEnemiesByRoom.Find(RoomTag);
	if (!RegisteredEnemies)
	{
		SearchStates.Remove(RoomTag);
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
			|| Candidate->GetRuntimeStat().Type == EEnemyType::Turret
			|| Candidate->GetRuntimeStat().MoveSpeed <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		EligibleEnemies.Add(Candidate);
	}

	if (EligibleEnemies.IsEmpty())
	{
		SearchStates.Remove(RoomTag);
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

	SearchStates.Add(RoomTag, MoveTemp(NewState));
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
	FGameplayTag RoomTag,
	const FVector& TargetLocation)
{
	UWorld* World = GetWorld();
	UOutlierArenaSubsystem* ArenaSubsystem = World
		? World->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	ULevel* ArenaLevel = ArenaSubsystem ? ArenaSubsystem->GetArenaLoadedLevel() : nullptr;
	if (!ArenaLevel)
	{
		return;
	}

	SearchStates.Remove(RoomTag);

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
		if (!IsValid(Enemy)
			|| Enemy->GetCombatState() != EEnemyCombatState::Combat
			|| !Enemy->CanUseRoomTargetSharing())
		{
			continue;
		}

		Enemy->ApplySharedTargetContact(TargetLocation);
	}
}

void UEnemyRoomSubsystem::BroadcastSharedTargetLost(FGameplayTag RoomTag)
{
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
	UWorld* World = GetWorld();
	CombatRooms.Reset();
	RegisteredEnemiesByRoom.Reset();
	RegisteredEnemyKeys.Reset();
	SearchStates.Reset();

	for (auto ContactIt = TargetContactStates.CreateIterator(); ContactIt; ++ContactIt)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ContactIt.Value().ForcedShareTimerHandle);
		}
	}
	TargetContactStates.Reset();
}
