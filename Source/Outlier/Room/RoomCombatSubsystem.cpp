#include "Room/RoomCombatSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Audio/OutlierAudioSubsystem.h"

#include "Enemy/AutoTurret.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/PlayerRelocationEQS.h"
#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierArenaSettings.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSpawnPoint.h"
#include "Room/RoomVolume.h"
#include "Room/RoomTagComponent.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GameplayTags/OutlierGameplayTags.h"

// 안전 판정의 반환값은 유지하고, 실패한 최초 조건만 진단용으로 전달한다.
enum class ERoomJoinRejection : uint8
{
	None,
	InvalidContext,
	CenterOutsideRoom,
	CapsuleOutsideRoom,
	AnchorOverlap,
	ExitBarrierOverlap,
	WorldBlockingCollision,
	GroundMissing,
	GroundTooSteep,
	Count
};

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
	const TCHAR* JoinRejectionName(ERoomJoinRejection Reason)
	{
		switch (Reason)
		{
		case ERoomJoinRejection::InvalidContext: return TEXT("InvalidContext");
		case ERoomJoinRejection::CenterOutsideRoom: return TEXT("CenterOutsideRoom");
		case ERoomJoinRejection::CapsuleOutsideRoom: return TEXT("CapsuleOutsideRoom");
		case ERoomJoinRejection::AnchorOverlap: return TEXT("AnchorOverlap");
		case ERoomJoinRejection::ExitBarrierOverlap: return TEXT("ExitBarrierOverlap");
		case ERoomJoinRejection::WorldBlockingCollision: return TEXT("WorldBlockingCollision");
		case ERoomJoinRejection::GroundMissing: return TEXT("GroundMissing");
		case ERoomJoinRejection::GroundTooSteep: return TEXT("GroundTooSteep");
		default: return TEXT("None");
		}
	}

	TAutoConsoleVariable<float> CVarInitialDetectionJoinTimeoutSeconds(
		TEXT("outlier.RoomCombat.InitialDetectionJoinTimeoutSeconds"), 5.0f,
		TEXT("Maximum total preparation seconds for initial detection EQS and designated fallback."));

	constexpr float SpawnRetryIntervalSeconds = 0.5f;
	TAutoConsoleVariable<float> CVarInitialDetectionRetrySeconds(
		TEXT("outlier.RoomCombat.InitialDetectionRetrySeconds"), 1.0f,
		TEXT("Minimum game-time interval before a new detection may retry a failed player join."));

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
					TEXT("[Reinforcement] Enemy=None Stage=WaveRejected Reason=InvalidRoster Room=%s Phase=%d Wave=%d Class=%s Count=%d"),
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
	OnMovementObstacleOpened.Clear();
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

void URoomCombatSubsystem::NotifyMovementObstacleOpened(const FBox& Bounds)
{
	if (!CanRunServerGameplay() || !Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()) return;
	OnMovementObstacleOpened.Broadcast(Bounds);
}

bool URoomCombatSubsystem::IsRoomStartHeld(FGameplayTag RoomTag) const
{
	if (bCheckpointCleanupInProgress) return true;
	const FRoomStartHold* Hold = RoomStartHolds.Find(RoomTag);
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Hold && Hold->Owner.IsValid()
		&& (!Hold->RoomRegistrationId.IsValid()
			|| (Runtime && Hold->RoomRegistrationId == Runtime->RegistrationId));
}

bool URoomCombatSubsystem::AcquireRoomStartHold(
	FGameplayTag RoomTag, FGuid ExecutionId, UObject* Owner)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!CanRunServerGameplay() || !RoomTag.IsValid() || !ExecutionId.IsValid() || !IsValid(Owner)
		|| Owner->GetWorld() != GetWorld())
	{
		return false;
	}
	if (IsRoomStartHeld(RoomTag))
	{
		const FRoomStartHold& Hold = RoomStartHolds.FindChecked(RoomTag);
		return Hold.ExecutionId == ExecutionId && Hold.Owner.Get() == Owner;
	}
	// 이미 시작한 Room을 중간 정지시키는 API가 아니다. 진입 전 예약에 실패하면 호출자가 이동을 보류한다.
	if (Runtime && (Runtime->State == ERoomCombatState::Preparing || Runtime->State == ERoomCombatState::Combat))
	{
		return false;
	}
	FRoomStartHold& Hold = RoomStartHolds.FindOrAdd(RoomTag);
	Hold.ExecutionId = ExecutionId;
	// 전투 정의가 없는 안전 도착 방도 예약한다. 나중에 Room이 등록되면 그 수명에 결합한다.
	Hold.RoomRegistrationId = Runtime ? Runtime->RegistrationId : FGuid();
	Hold.Owner = Owner;
	return true;
}

void URoomCombatSubsystem::ReleaseRoomStartHold(FGuid ExecutionId)
{
	if (!CanRunServerGameplay() || !ExecutionId.IsValid())
	{
		return;
	}
	// 재평가가 외부 콜백을 실행할 수 있으므로 먼저 예약을 모두 제거하고 등록 수명만 복사한다.
	TArray<TPair<FGameplayTag, FGuid>> ReleasedRooms;
	for (auto It = RoomStartHolds.CreateIterator(); It; ++It)
	{
		if (It.Value().ExecutionId == ExecutionId)
		{
			ReleasedRooms.Emplace(It.Key(), It.Value().RoomRegistrationId);
			It.RemoveCurrent();
		}
	}
	for (const TPair<FGameplayTag, FGuid>& Room : ReleasedRooms)
	{
		ResumeRoomAfterStartHold(Room.Key, Room.Value);
	}
}

void URoomCombatSubsystem::ResumeRoomAfterStartHold(FGameplayTag RoomTag, FGuid RegistrationId)
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag) || !Runtime
		|| Runtime->RegistrationId != RegistrationId || !Runtime->RoomVolume.IsValid())
	{
		return;
	}
	TryStartRestoredAutomaticPhase(RoomTag);
	// 각 경로가 델리게이트를 실행할 수 있다. 이전 Runtime 포인터를 다음 시작 요청에 재사용하지 않는다.
	Runtime = RoomRuntimes.Find(RoomTag);
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag) || !Runtime
		|| Runtime->RegistrationId != RegistrationId)
	{
		return;
	}
	if (IsInitialDetectionPhase(RoomTag, *Runtime))
	{
		TryStartInitialDetectionForPlayers(RoomTag);
	}
	else
	{
		StartAutomaticPhase(RoomTag);
	}
	Runtime = RoomRuntimes.Find(RoomTag);
	if (CanRunServerGameplay() && !IsRoomStartHeld(RoomTag) && Runtime
		&& Runtime->RegistrationId == RegistrationId)
	{
		// 외부 트리거는 보류 중 요청을 재생하지 않고 현재 시작 가능 여부만 다시 알린다.
		OnRoomStartReadinessChanged.Broadcast(RoomTag);
	}
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
	return !Runtime.bPhaseCleanupInProgress
		&& Runtime.State == ERoomCombatState::Combat && IsRoomCombatReserved(RoomTag);
}

bool URoomCombatSubsystem::CanReserveCombatRoom(FGameplayTag RoomTag) const
{
	if (!RoomTag.IsValid()) return false;
	if (ReservedCombatRooms.IsEmpty() || ReservedCombatRooms.Contains(RoomTag)) return true;
	const FGameplayTag GroupTag = URoomTagComponent::GetRoomGroupTag(RoomTag);
	// 기존 세 단계 태그는 자기 방만 허용한다. 새 그룹 부모와 이름이 같아도
	// 형제 방으로 섞이지 않도록 유효한 네 단계 자식끼리만 그룹을 비교한다.
	return GroupTag.IsValid() && GroupTag == ActiveCombatGroupTag;
}

void URoomCombatSubsystem::ReserveCombatRoom(FGameplayTag RoomTag)
{
	check(CanReserveCombatRoom(RoomTag));
	ReservedCombatRooms.Add(RoomTag);
	if (!ActiveCombatRoomTag.IsValid())
	{
		ActiveCombatGroupId = FGuid::NewGuid();
		ActiveCombatRoomTag = RoomTag;
		ActiveCombatGroupTag = URoomTagComponent::GetRoomGroupTag(RoomTag);
	}
}

void URoomCombatSubsystem::ReleaseCombatRoom(FGameplayTag RoomTag)
{
	ReservedCombatRooms.Remove(RoomTag);
	if (ReservedCombatRooms.IsEmpty())
	{
		ActiveCombatGroupId.Invalidate();
		ActiveCombatRoomTag = FGameplayTag();
		ActiveCombatGroupTag = FGameplayTag();
	}
	else if (ActiveCombatRoomTag == RoomTag)
	{
		// 대표 방이 끝나도 그룹은 유지한다. 진단/기존 단일 방 소비자의 선택은 결정적으로 한다.
		TArray<FGameplayTag> RemainingRooms = ReservedCombatRooms.Array();
		RemainingRooms.Sort([](FGameplayTag Left, FGameplayTag Right)
		{
			return Left.GetTagName().LexicalLess(Right.GetTagName());
		});
		ActiveCombatRoomTag = RemainingRooms[0];
	}
}

bool URoomCombatSubsystem::IsInitialDetectionPhase(
	FGameplayTag RoomTag,
	const FRoomCombatRuntime& Runtime) const
{
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Definition
		? Definition->FindPhase(Runtime.CurrentCombatPhaseIndex)
		: nullptr;
	return Phase && Phase->StartPolicy == ERoomCombatPhaseStartPolicy::InitialDetection;
}

void URoomCombatSubsystem::StartInitialDetectionCombat(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	if (IsRoomStartHeld(RoomTag))
	{
		return;
	}
	// 기존 즉시 시작과 합류 준비 완료가 같은 Wave 시작 경로를 사용한다.
	Runtime.PreparingAnchorPlayer.Reset();
	Runtime.PreparationId.Invalidate();
	Runtime.bInitialDetectionPendingJoin = false;
	Runtime.bPreparationOwnsReservation = false;
	Runtime.State = ERoomCombatState::Combat;
	Runtime.CurrentWaveIndex = 0;
	Runtime.bCurrentWaveSpawnStarted = true;
	ReserveCombatRoom(RoomTag);
	// 배치 Wave는 별도 Spawn 요청이 없으므로 전투 진입 자체가 Wave 시작 완료 시점이다.
	FinalizeCurrentWaveSpawn(RoomTag, Runtime);
}

bool URoomCombatSubsystem::CreateTriggerContext(
	AActor* Requester, FGameplayTag RoomTag, FGameplayTag ActivationGroupTag,
	FRoomCombatTriggerContext& OutContext) const
{
	OutContext = FRoomCombatTriggerContext();
	// 외부 시작 요청 당시의 수명만 기록한다. Room을 예약하지 않으므로 실행 시 다시 검사한다.
	UWorld* World = GetWorld();
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* Phase = Definition && Runtime
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
		: nullptr;
	// 점유 그룹 안의 형제 Room만 추가로 시작할 수 있다. 각 방의 트리거 조건은 따로 검사한다.
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag)
		|| !IsValid(Requester) || Requester->IsActorBeingDestroyed()
		|| !Requester->HasAuthority() || Requester->GetWorld() != World
		|| !Runtime || !Runtime->RoomVolume.IsValid()
		|| (Runtime->CheckpointHoldOwner.IsValid() && Runtime->CheckpointHoldOwner.Get() != Requester)
		|| Runtime->State != ERoomCombatState::WaitingForTrigger
		|| !CanReserveCombatRoom(RoomTag)
		|| !Phase
		|| (Phase->StartPolicy != ERoomCombatPhaseStartPolicy::HackTrigger
			&& Phase->StartPolicy != ERoomCombatPhaseStartPolicy::ExternalTrigger)
		|| (Phase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger
			&& !ActivationGroupTag.IsValid()))
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

bool URoomCombatSubsystem::HasCurrentWaveSpawned(const FRoomCombatTriggerContext& Context) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
	return CanRunServerGameplay() && Runtime && Runtime->bCurrentWaveHasSpawned
		&& Runtime->State == ERoomCombatState::Combat
		&& Runtime->RegistrationId == Context.RoomRegistrationId
		&& Runtime->GameplayGeneration == Context.GameplayGeneration
		&& Runtime->CurrentCombatPhaseIndex == Context.CombatPhaseIndex;
}

bool URoomCombatSubsystem::IsSingleExternalTriggerEncounter(FGameplayTag RoomTag) const
{
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	if (!Definition || Definition->CombatPhases.Num() != 1 || !Definition->CanStartTriggeredSequence(0))
	{
		return false;
	}
	const FRoomCombatPhaseDefinition& Phase = Definition->CombatPhases[0];
	return Phase.StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger;
}

int32 URoomCombatSubsystem::GetFirstExternalTriggerPhaseIndex(FGameplayTag RoomTag) const
{
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	if (Definition && Definition->HasValidPhaseOrder())
	{
		for (int32 Index = 0; Index < Definition->CombatPhases.Num(); ++Index)
		{
			if (Definition->CombatPhases[Index].StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

bool URoomCombatSubsystem::FinishInitialPhaseForUpgrade(ARoomVolume* RoomVolume, int32 GameplayGeneration)
{
	if (bCheckpointCleanupInProgress) return false;
	return FinishCombatWithCleanup(RoomVolume, GameplayGeneration, false);
}

bool URoomCombatSubsystem::CollectCheckpointCleanupRooms(
	TArray<FRoomCombatTriggerContext>& OutRooms, FString& OutReason) const
{
	OutRooms.Reset();
	OutReason.Reset();
	if (!CanRunServerGameplay() || bCheckpointCleanupInProgress)
	{
		OutReason = TEXT("Authority, reset or cleanup in progress");
		return false;
	}
	TSet<FGameplayTag> Candidates = ReservedCombatRooms;
	for (const auto& Hold : RoomStartHolds)
	{
		if (IsRoomStartHeld(Hold.Key))
		{
			OutReason = TEXT("Room start held by an active transition");
			return false;
		}
	}
	for (const auto& Entry : RoomRuntimes)
	{
		const FRoomCombatRuntime& Runtime = Entry.Value;
		if (Runtime.CheckpointHoldExecutionId.IsValid() && Runtime.CheckpointHoldOwner.IsValid())
		{
			OutReason = TEXT("Encounter checkpoint held by an active transition");
			return false;
		}
		if (Runtime.State == ERoomCombatState::Combat || Runtime.State == ERoomCombatState::Preparing
			|| Runtime.bTriggeredSequenceActive || Runtime.PendingActivationCount > 0)
		{
			Candidates.Add(Entry.Key);
		}
	}
	for (const FRoomCombatPendingSpawn& Request : PendingSpawnRequests) Candidates.Add(Request.RoomTag);
	// EnemyRoom에는 RoomCombat 등록이 없는 전투도 존재할 수 있다. 그 적은 제거할 수 없다.
	if (const UEnemyRoomSubsystem* Enemies = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		TArray<AEnemyBase*> ActiveEnemies;
		Enemies->GetActiveCombatEnemies(ActiveEnemies);
		for (AEnemyBase* Enemy : ActiveEnemies)
		{
			const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(Enemy);
			if (!Registration)
			{
				OutReason = FString::Printf(TEXT("Unregistered combat enemy %s"), *GetNameSafe(Enemy));
				return false;
			}
			Candidates.Add(Registration->RoomTag);
		}
	}
	const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	for (FGameplayTag Tag : Candidates)
	{
		const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Tag);
		const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(Tag);
		ARoomVolume* Room = Runtime ? Runtime->RoomVolume.Get() : nullptr;
		if (!Runtime || !IsValid(Room) || Room->IsActorBeingDestroyed() || !Room->HasAuthority()
			|| !Runtime->RegistrationId.IsValid() || !Runtime->bEncounterIdRegistered
			|| !Definition || !Definition->bFinishCombatOnCheckpointSave || !Definition->HasValidPhaseOrder()
			|| !Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
			|| Runtime->State == ERoomCombatState::Cleared || Runtime->bPhaseCleanupInProgress
			|| Runtime->CheckpointHoldExecutionId.IsValid() || IsRoomStartHeld(Tag)
			|| (Arena && Runtime->GameplayGeneration != static_cast<int32>(Arena->GetGameplayGeneration())))
		{
			OutReason = FString::Printf(TEXT("Room not eligible for checkpoint cleanup: %s"), *Tag.ToString());
			return false;
		}
		FRoomCombatTriggerContext& Context = OutRooms.AddDefaulted_GetRef();
		Context.RoomTag = Tag;
		Context.RoomRegistrationId = Runtime->RegistrationId;
		Context.GameplayGeneration = Runtime->GameplayGeneration;
		Context.CombatPhaseIndex = Runtime->CurrentCombatPhaseIndex;
	}
	return true;
}

bool URoomCombatSubsystem::CanFinishCombatForCheckpoint(FString& OutReason) const
{
	TArray<FRoomCombatTriggerContext> Rooms;
	return CollectCheckpointCleanupRooms(Rooms, OutReason);
}

bool URoomCombatSubsystem::FinishCombatForCheckpoint()
{
	TArray<FRoomCombatTriggerContext> Rooms;
	FString Reason;
	if (!CollectCheckpointCleanupRooms(Rooms, Reason))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] Combat cleanup preflight failed: %s"), *Reason);
		return false;
	}
	TGuardValue<bool> Guard(bCheckpointCleanupInProgress, true);
	auto IsCurrent = [this](const FRoomCombatTriggerContext& Context)
	{
		const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
		const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
		return CanRunServerGameplay() && Runtime && Runtime->RoomVolume.IsValid()
			&& Runtime->RegistrationId == Context.RoomRegistrationId
			&& Runtime->GameplayGeneration == Context.GameplayGeneration
			&& (!Arena || static_cast<int32>(Arena->GetGameplayGeneration()) == Context.GameplayGeneration);
	};
	for (const FRoomCombatTriggerContext& Context : Rooms)
	{
		if (!IsCurrent(Context)
			|| RoomRuntimes.FindChecked(Context.RoomTag).CurrentCombatPhaseIndex != Context.CombatPhaseIndex
			|| !FinishCombatWithCleanup(RoomRuntimes.FindChecked(Context.RoomTag).RoomVolume.Get(),
				Context.GameplayGeneration, true))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] Combat cleanup interrupted; world may be partially cleaned. Room=%s"),
				*Context.RoomTag.ToString());
			return false;
		}
	}
	for (const FRoomCombatTriggerContext& Context : Rooms)
	{
		if (!IsCurrent(Context) || GetRoomState(Context.RoomTag) != ERoomCombatState::Cleared) return false;
	}
	const UEnemyRoomSubsystem* Enemies = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	return !HasActiveCombatReservation() && (!Enemies || !Enemies->HasActiveCombat());
}

#if WITH_EDITOR
bool URoomCombatSubsystem::FinishRoomCombatForEditor(FGameplayTag RoomTag)
{
	if (bCheckpointCleanupInProgress) return false;
	if (!GetWorld() || GetWorld()->WorldType != EWorldType::PIE || !CanRunServerGameplay()) return false;
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && FinishCombatWithCleanup(Runtime->RoomVolume.Get(), Runtime->GameplayGeneration, true);
}
#endif

bool URoomCombatSubsystem::FinishCombatWithCleanup(ARoomVolume* RoomVolume, int32 GameplayGeneration, bool bFinishRoom)
{
	if (!CanRunServerGameplay() || !IsValid(RoomVolume) || RoomVolume->IsActorBeingDestroyed()
		|| RoomVolume->GetWorld() != GetWorld() || !RoomVolume->HasAuthority())
	{
		return false;
	}
	const FGameplayTag RoomTag = RoomVolume->GetRoomTag();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex) : nullptr;
	const FRoomCombatPhaseDefinition* NextPhase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex + 1) : nullptr;
	const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Runtime || Runtime->RoomVolume.Get() != RoomVolume || Runtime->bPhaseCleanupInProgress
		|| Runtime->GameplayGeneration != GameplayGeneration
		|| (Arena && static_cast<int32>(Arena->GetGameplayGeneration()) != GameplayGeneration)
		|| !Definition || !Definition->HasValidPhaseOrder() || !Phase
		|| (!bFinishRoom && (!NextPhase || Phase->StartPolicy != ERoomCombatPhaseStartPolicy::InitialDetection
		|| !Phase->bReleaseCombatOnCompletion
		|| NextPhase->StartPolicy != ERoomCombatPhaseStartPolicy::ExternalTrigger))
		|| Runtime->State == ERoomCombatState::Cleared
		|| (!bFinishRoom && Runtime->State == ERoomCombatState::WaitingForTrigger))
	{
		return false;
	}
	const int32 PhaseIndex = Runtime->CurrentCombatPhaseIndex;
	const FGuid RegistrationId = Runtime->RegistrationId;
	auto IsCleanupCurrent = [this, RoomTag, RoomVolume, GameplayGeneration, PhaseIndex, RegistrationId]()
	{
		const FRoomCombatRuntime* Current = RoomRuntimes.Find(RoomTag);
		const UOutlierArenaSubsystem* CurrentArena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
		return Current && Current->RoomVolume.Get() == RoomVolume
			&& Current->RegistrationId == RegistrationId && Current->GameplayGeneration == GameplayGeneration
			&& (!CurrentArena || static_cast<int32>(CurrentArena->GetGameplayGeneration()) == GameplayGeneration)
			&& Current->CurrentCombatPhaseIndex == PhaseIndex && Current->bPhaseCleanupInProgress;
	};

	// 1. 진행과 지연 작업을 먼저 닫는다. 제거 중 발생하는 사망/전개 콜백은 차수를 넘길 수 없다.
	Runtime->bPhaseCleanupInProgress = true;
	Runtime->PreparationId.Invalidate();
	Runtime->PreparingAnchorPlayer.Reset();
	Runtime->bInitialDetectionPendingJoin = false;
	Runtime->bPreparationOwnsReservation = false;
	Runtime->bPreparationExitBlockWasActive = false;
	Runtime->bPhaseDelayPending = false;
	Runtime->bDeferSpawnExecution = false;
	GetWorld()->GetTimerManager().ClearTimer(Runtime->InitialSpawnDelayTimer);
	GetWorld()->GetTimerManager().ClearTimer(Runtime->PhaseDelayTimer);
	PendingSpawnRequests.RemoveAll([RoomTag, PhaseIndex, bFinishRoom](const FRoomCombatPendingSpawn& Request)
	{
		return Request.RoomTag == RoomTag && (bFinishRoom || Request.CombatPhaseIndex == PhaseIndex);
	});
	Runtime->SpawnAssignments.Reset();
	Runtime->PendingActivationCount = 0;
	Runtime->PendingWaveTurretActivations.Reset();
	RefreshSpawnPointWorkStates(RoomTag, false);
	if (PendingSpawnRequests.IsEmpty()) CancelSpawnRetry();

	struct FEnemyCleanup
	{
		TWeakObjectPtr<AEnemyBase> Enemy;
		bool bPoolManaged;
		int32 PoolGeneration;
		int32 LeaseSerial;
	};
	TArray<FEnemyCleanup> Enemies;
	// 2. 아군화된 적도 전투 소속으로 찾는다. 모든 등록을 먼저 끊어 EndPlay가 전투 완료로 집계되지 않게 한다.
	for (auto It = RegisteredEnemies.CreateIterator(); It; ++It)
	{
		if (It.Value().RoomTag != RoomTag || (!bFinishRoom && It.Value().CombatPhaseIndex != PhaseIndex)) continue;
		if (AEnemyBase* Enemy = It.Key().Get())
		{
			Enemies.Add({ Enemy, It.Value().bPoolManaged,
				Enemy->GetPoolGameplayGeneration(), Enemy->GetPoolLeaseSerial() });
			Runtime->TrackedAliveEnemies.Remove(Enemy);
			if (TSet<TWeakObjectPtr<AEnemyBase>>* Pending = PendingPreplacedEnemies.Find(RoomTag))
			{
				Pending->Remove(Enemy);
			}
		}
		It.RemoveCurrent();
	}
	TArray<TWeakObjectPtr<AAutoTurret>> Turrets;
	if (TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RoomTag))
	{
		for (const FRoomCombatWaveTurretRuntime& Entry : *Entries)
		{
			if (bFinishRoom || Entry.CombatPhaseIndex == PhaseIndex) Turrets.Add(Entry.Turret);
		}
	}
	for (const TWeakObjectPtr<AAutoTurret>& Turret : Turrets)
	{
		if (Turret.IsValid()) UnregisterWaveTurret(Turret.Get());
	}
	ClearForcedJoinProtectionsForRoom(RoomTag);
	if (!IsCleanupCurrent()) return false;

	// 3. 처치/피해 API를 사용하지 않는다. 빙의는 원래 Partner로 복구하고 풀 대여 토큰까지 확인한다.
	UEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UEnemyPoolSubsystem>();
	for (const FEnemyCleanup& Entry : Enemies)
	{
		if (!IsCleanupCurrent()) return false;
		AEnemyBase* Enemy = Entry.Enemy.Get();
		if (!Enemy || (Entry.bPoolManaged && !Enemy->MatchesPoolLease(Entry.PoolGeneration, Entry.LeaseSerial))) continue;
		Enemy->CancelPossessionProcess();
		if (!IsCleanupCurrent()) return false;
		Enemy = Entry.Enemy.Get();
		if (!Enemy || (Entry.bPoolManaged && !Enemy->MatchesPoolLease(Entry.PoolGeneration, Entry.LeaseSerial))) continue;
		if (APartnerPlayerController* Controller = Cast<APartnerPlayerController>(Enemy->GetController()))
		{
			Controller->ReleaseEnemyPossessionForCleanup();
		}
		if (!IsCleanupCurrent()) return false;
		Enemy = Entry.Enemy.Get();
		if (!Enemy || (Entry.bPoolManaged && !Enemy->MatchesPoolLease(Entry.PoolGeneration, Entry.LeaseSerial))) continue;
		if (Entry.bPoolManaged)
		{
			const bool bReturned = Pool && Pool->ReturnEnemy(Enemy, Entry.PoolGeneration, Entry.LeaseSerial);
			// 손상된 풀 등록 때문에 반환에 실패해도 적을 월드에 남기지는 않는다.
			// 콜백에서 새로 대여된 Actor는 이전 토큰으로 제거하지 않는다.
			Enemy = Entry.Enemy.Get();
			if (!bReturned && IsCleanupCurrent() && Enemy
				&& Enemy->MatchesPoolLease(Entry.PoolGeneration, Entry.LeaseSerial))
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoomCombat] Upgrade cleanup pool return failed; removing old lease. Enemy=%s Room=%s"),
					*GetNameSafe(Enemy), *RoomTag.ToString());
				Enemy->Destroy();
			}
		}
		else
		{
			Enemy->Destroy();
		}
	}
	for (const TWeakObjectPtr<AAutoTurret>& Turret : Turrets)
	{
		if (!IsCleanupCurrent()) return false;
		if (Turret.IsValid()) Turret->Destroy();
	}
	if (!IsCleanupCurrent()) return false;

	// 4. 정리가 끝난 뒤 기존 저장/알림 경로로 확정한다. 테스터는 다음 차수도 건너뛰고 방을 완료한다.
	Runtime = RoomRuntimes.Find(RoomTag);
	Runtime->bPhaseCleanupInProgress = false;
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Combat cleanup finished. Room=%s Phase=%d Generation=%d Enemies=%d Turrets=%d FinishRoom=%d"),
		*RoomTag.ToString(), PhaseIndex, GameplayGeneration, Enemies.Num(), Turrets.Num(), bFinishRoom);
	CompleteCurrentPhase(RoomTag, true, bFinishRoom);
	return true;
}

bool URoomCombatSubsystem::IsElevatorEncounter(FGameplayTag RoomTag) const
{
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	if (!Definition || !Definition->CanStartTriggeredSequence(0)
		|| Definition->CombatPhases[0].StartPolicy != ERoomCombatPhaseStartPolicy::ExternalTrigger)
	{
		return false;
	}
	// 일반 전투는 후속 외부 Trigger를 허용하지만, 운행 중에는 자동 차수만 이어간다.
	for (int32 Index = 1; Index < Definition->CombatPhases.Num(); ++Index)
	{
		if (Definition->CombatPhases[Index].StartPolicy != ERoomCombatPhaseStartPolicy::Automatic)
		{
			return false;
		}
	}
	return true;
}

bool URoomCombatSubsystem::AcquireEncounterCheckpointHold(
	const FRoomCombatTriggerContext& Context, FGuid ExecutionId)
{
	FRoomCombatTriggerContext Current;
	if (!ExecutionId.IsValid() || !IsElevatorEncounter(Context.RoomTag)
		|| !CreateTriggerContext(Context.Requester.Get(), Context.RoomTag, Context.ActivationGroupTag, Current)
		|| Current.CombatPhaseIndex != 0 || Context.CombatPhaseIndex != 0
		|| Current.RoomRegistrationId != Context.RoomRegistrationId
		|| Current.GameplayGeneration != Context.GameplayGeneration)
	{
		return false;
	}
	FRoomCombatRuntime& Runtime = RoomRuntimes.FindChecked(Context.RoomTag);
	if (Runtime.CheckpointHoldOwner.IsValid()
		&& (Runtime.CheckpointHoldOwner != Context.Requester || Runtime.CheckpointHoldExecutionId != ExecutionId))
	{
		return false;
	}
	Runtime.CheckpointHoldOwner = Context.Requester;
	Runtime.CheckpointHoldExecutionId = ExecutionId;
	return true;
}

void URoomCombatSubsystem::ReleaseEncounterCheckpointHold(FGameplayTag RoomTag, FGuid ExecutionId)
{
	if (FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
		Runtime && Runtime->CheckpointHoldExecutionId == ExecutionId)
	{
		Runtime->CheckpointHoldOwner.Reset();
		Runtime->CheckpointHoldExecutionId.Invalidate();
	}
}

bool URoomCombatSubsystem::IsEncounterContextCurrent(
	const FRoomCombatTriggerContext& Context, FGuid ExecutionId) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
	return CanRunServerGameplay() && Runtime && Runtime->RoomVolume.IsValid()
		&& Runtime->RegistrationId == Context.RoomRegistrationId
		&& Runtime->GameplayGeneration == Context.GameplayGeneration
		&& ExecutionId.IsValid() && Runtime->CheckpointHoldExecutionId == ExecutionId
		&& Context.Requester.IsValid() && Runtime->CheckpointHoldOwner == Context.Requester;
}

bool URoomCombatSubsystem::IsCombatPhaseCheckpointDeferred(FGameplayTag RoomTag, int32 Generation) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return CanRunServerGameplay() && Runtime && Runtime->RoomVolume.IsValid()
		&& Runtime->GameplayGeneration == Generation && Runtime->CheckpointHoldExecutionId.IsValid()
		&& Runtime->CheckpointHoldOwner.IsValid() && !Runtime->CheckpointHoldOwner->IsActorBeingDestroyed();
}

bool URoomCombatSubsystem::StartTriggeredSequence(
	AActor* Requester, const FRoomCombatTriggerContext& Context)
{
	return StartTriggeredSequenceWithSpawnDelay(Requester, Context, 0.0f);
}

bool URoomCombatSubsystem::StartTriggeredSequenceWithSpawnDelay(
	AActor* Requester, const FRoomCombatTriggerContext& Context, float SpawnDelaySeconds)
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
	if (Runtime && Runtime->bPhaseDelayPending)
	{
		// 문맥 조회는 미리 허용하지만, 조기 성공 요청을 예약하거나 나중에 자동 재생하지 않는다.
		UE_LOG(LogTemp, Verbose, TEXT("[RoomCombat] Trigger rejected: phase delay pending. Room=%s Phase=%d Remaining=%.3f"),
			*Context.RoomTag.ToString(), Context.CombatPhaseIndex, GetPhaseDelayRemainingSeconds(Context.RoomTag));
		return false;
	}
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(Context.RoomTag);
	if (!Definition)
	{
		return false;
	}
	if (!Definition->CanStartTriggeredSequence(Context.CombatPhaseIndex)
		|| HasPendingSpawns(Context.RoomTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomCombat] Invalid triggered sequence. Room=%s Phase=%d"),
			*Context.RoomTag.ToString(), Context.CombatPhaseIndex);
		return false;
	}
	// 해킹/외부 시작은 호출자가 성공 반환 후 진행을 소비한다. 비동기 안전 준비가
	// 필요하면 이번 요청은 실패로 남기고, 안전해진 뒤 호출자의 새 요청만 수락한다.
	const TWeakObjectPtr<AActor> WeakRequester(Requester);
	if (!PrepareBarrierClosure(Context.RoomTag, [this, WeakRequester, Context]
	{
		FRoomCombatTriggerContext Current;
		return WeakRequester.IsValid() && CreateTriggerContext(WeakRequester.Get(), Context.RoomTag,
			Context.ActivationGroupTag, Current) && Current.RoomRegistrationId == Context.RoomRegistrationId
			&& Current.GameplayGeneration == Context.GameplayGeneration
			&& Current.CombatPhaseIndex == Context.CombatPhaseIndex;
	}, [](bool) {})) return false;

	// 후속 외부 차수에서 시작이 실패해도 앞선 차수의 출입 차단 상태를 보존한다.
	const bool bWasExitBlocked = Runtime->bExitBlockActive;
	Runtime->State = ERoomCombatState::Combat;
	Runtime->CurrentWaveIndex = 0;
	Runtime->bTriggeredSequenceActive = true;
	Runtime->bExitBlockActive = true;
	Runtime->bDeferSpawnExecution = true;
	Runtime->ActiveActivationGroupTag = Context.ActivationGroupTag;
	ReserveCombatRoom(Context.RoomTag);
	SetActivationGroupActive(Context.RoomTag, Context.ActivationGroupTag, true);
	if (!StartWaveSpawning(Context.RoomTag, Context.CombatPhaseIndex, 0))
	{
		Runtime->State = ERoomCombatState::WaitingForTrigger;
		Runtime->bTriggeredSequenceActive = false;
		Runtime->bExitBlockActive = bWasExitBlocked;
		Runtime->bDeferSpawnExecution = false;
		Runtime->ActiveActivationGroupTag = FGameplayTag();
		SetActivationGroupActive(Context.RoomTag, Context.ActivationGroupTag, false);
		if (!bWasExitBlocked)
		{
			ReleaseCombatRoom(Context.RoomTag);
		}
		return false;
	}

	// 차단막이 시작 이벤트를 반영한 뒤 소환한다. 이벤트 중 Reset됐다면 재개하지 않는다.
	// 호출자가 저장한 Context도 이벤트 중 바뀔 수 있으므로 검증된 복사본으로 재개한다.
	BroadcastCombatEvent(CurrentContext.RoomTag, ERoomCombatEvent::SequenceStarted,
		CurrentContext.CombatPhaseIndex, CurrentContext.GameplayGeneration);
	Runtime = RoomRuntimes.Find(CurrentContext.RoomTag);
	if (Runtime && Runtime->RegistrationId == CurrentContext.RoomRegistrationId
		&& Runtime->State == ERoomCombatState::Combat && Runtime->RoomVolume.IsValid())
	{
		// 해킹 시작에는 발각 Enemy가 없을 수 있다. 차단막 이벤트 이후, 실제 소환 전에
		// 방의 AI 전투도 열어 새 Pool 대여가 즉시 Combat으로 동기화되게 한다.
		const ARoomVolume* Room = Runtime->RoomVolume.Get();
		FVector TargetLocation = Room->GetActorLocation();
		bool bFoundPlayer = false;
		if (const AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				const AOutlierPlayerState* OutlierState = Cast<AOutlierPlayerState>(PlayerState);
				if (!OutlierState)
				{
					continue;
				}
				for (const AFirstPersonCharacter* Player : {
					static_cast<const AFirstPersonCharacter*>(OutlierState->GetShooterCharacter()),
					static_cast<const AFirstPersonCharacter*>(OutlierState->GetPartnerCharacter())})
				{
					if (IsPlayerInsideRoom(Player, Room))
					{
						TargetLocation = Player->GetActorLocation();
						bFoundPlayer = true;
						break;
					}
				}
				if (bFoundPlayer)
				{
					break;
				}
			}
		}
		if (UEnemyRoomSubsystem* EnemyRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			const bool bRoomSynced = EnemyRooms->NotifyRoomCombat(
				CurrentContext.RoomTag, TargetLocation, nullptr);
			UE_LOG(LogTemp, Display,
				TEXT("[Reinforcement] Enemy=None Stage=RoomCombatSync Room=%s Generation=%d Ready=%d PlayerInRoom=%d Target=%s"),
				*CurrentContext.RoomTag.ToString(), CurrentContext.GameplayGeneration,
				bRoomSynced, bFoundPlayer, *TargetLocation.ToCompactString());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Reinforcement] Enemy=None Stage=RoomCombatSync Room=%s Generation=%d Ready=0 Reason=EnemyRoomSubsystemMissing"),
				*CurrentContext.RoomTag.ToString(), CurrentContext.GameplayGeneration);
		}
	}
	Runtime = RoomRuntimes.Find(CurrentContext.RoomTag);
	if (SpawnDelaySeconds > 0.0f && Runtime
		&& Runtime->RegistrationId == CurrentContext.RoomRegistrationId
		&& Runtime->State == ERoomCombatState::Combat && Runtime->bDeferSpawnExecution)
	{
		GetWorld()->GetTimerManager().SetTimer(Runtime->InitialSpawnDelayTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, CurrentContext]()
			{
				ResumeDeferredSpawning(CurrentContext.RoomTag, CurrentContext.RoomRegistrationId);
			}), SpawnDelaySeconds, false);
	}
	else
	{
		ResumeDeferredSpawning(CurrentContext.RoomTag, CurrentContext.RoomRegistrationId);
	}
	return true;
}

bool URoomCombatSubsystem::IsExitBlocked(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && Runtime->bExitBlockActive;
}

bool URoomCombatSubsystem::IsGroupExitBlocked(FGameplayTag RoomTag) const
{
	if (!URoomTagComponent::IsValidChildRoomTag(RoomTag))
	{
		return IsExitBlocked(RoomTag);
	}
	// Preparing 예약은 아직 차단 원인이 아니다. 기존 방별 플래그가 실제 전투
	// 시작부터 차수 대기까지 유지되므로 참여/완료 명단을 별도로 복제하지 않는다.
	for (const auto& Entry : RoomRuntimes)
	{
		if (Entry.Value.bExitBlockActive && URoomTagComponent::AreRoomsInSameGroup(RoomTag, Entry.Key))
		{
			return true;
		}
	}
	return false;
}

bool URoomCombatSubsystem::IsPhaseDelayPending(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && Runtime->bPhaseDelayPending;
}

float URoomCombatSubsystem::GetPhaseDelayRemainingSeconds(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const UWorld* World = GetWorld();
	if (!Runtime || !Runtime->bPhaseDelayPending || !World)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(Runtime->PhaseDelayTimer));
}

int32 URoomCombatSubsystem::GetPendingCombatPhaseIndex(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return Runtime && Runtime->bPhaseDelayPending ? Runtime->CurrentCombatPhaseIndex : INDEX_NONE;
}

FGameplayTag URoomCombatSubsystem::FindBarrierClosureRoom(const ARoomCombatBarrier* Barrier) const
{
	if (!IsValid(Barrier)) return FGameplayTag();
	TArray<FGameplayTag> BoundRooms;
	Barrier->GetSafetyRoomTags(BoundRooms);
	// 공유 출입구가 그룹 태그만 갖는 경우에도 실제 등록되어 전투 중인 방을 찾는다.
	// 공간상 가까운 방 대신 명시적 연결과 현재 전투 문맥을 기준으로 선택한다.
	for (FGameplayTag BoundRoom : BoundRooms)
	{
		if (GetRegisteredRoomVolume(BoundRoom) && IsGroupExitBlocked(BoundRoom)) return BoundRoom;
		for (const auto& Entry : RoomRuntimes)
		{
			if (Entry.Value.RoomVolume.IsValid() && Entry.Value.bExitBlockActive
				&& URoomTagComponent::AreRoomsInSameGroup(BoundRoom, Entry.Key)
				&& Barrier->CanBlockRoom(Entry.Key)) return Entry.Key;
		}
	}
	return FGameplayTag();
}

bool URoomCombatSubsystem::HasBarrierOccupants(FGameplayTag RoomTag) const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	if (!State) return false;
	for (APlayerState* PlayerState : State->PlayerArray)
	{
		const AOutlierPlayerState* Pair = Cast<AOutlierPlayerState>(PlayerState);
		if (!Pair) continue;
		for (const AFirstPersonCharacter* Player : {
			static_cast<const AFirstPersonCharacter*>(Pair->GetShooterCharacter()),
			static_cast<const AFirstPersonCharacter*>(Pair->GetPartnerCharacter())})
		{
			if (!IsValid(Player) || Player->IsActorBeingDestroyed()) continue;
			const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
			for (const auto& Entry : RegisteredBarriers)
				if (const ARoomCombatBarrier* Barrier = Entry.Get(); IsValid(Barrier)
					&& Barrier->CanBlockRoom(RoomTag) && Capsule
					&& Barrier->OverlapsJoinCapsule(Capsule->GetComponentLocation(),
						Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight())) return true;
		}
	}
	return false;
}

bool URoomCombatSubsystem::PrepareBarrierClosure(FGameplayTag RoomTag,
	TFunction<bool()> IsCurrent, TFunction<void(bool)> Completion)
{
	if (!CanRunServerGameplay() || !IsCurrent || !IsCurrent()) return false;
	if (BarrierClosurePreparations.Contains(RoomTag)) return false;
	if (!HasBarrierOccupants(RoomTag)) return true;
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || !Runtime->RoomVolume.IsValid()) return false;
	FBarrierClosurePreparation Work;
	Work.Id = FGuid::NewGuid();
	Work.RegistrationId = Runtime->RegistrationId;
	Work.Generation = Runtime->GameplayGeneration;
	Work.IsCurrent = MoveTemp(IsCurrent);
	Work.Completion = MoveTemp(Completion);
	const AGameStateBase* State = GetWorld()->GetGameState();
	for (APlayerState* PlayerState : State->PlayerArray)
	{
		const AOutlierPlayerState* Pair = Cast<AOutlierPlayerState>(PlayerState);
		if (!Pair) continue;
		for (AFirstPersonCharacter* Player : {
			static_cast<AFirstPersonCharacter*>(Pair->GetShooterCharacter()),
			static_cast<AFirstPersonCharacter*>(Pair->GetPartnerCharacter())})
		{
			if (!IsValid(Player) || Work.Players.Contains(Player)) continue;
			const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
			if (!Capsule) continue;
			ARoomVolume* DestinationRoom = nullptr;
			for (const auto& Entry : RegisteredBarriers)
			{
				const ARoomCombatBarrier* Barrier = Entry.Get();
				if (!IsValid(Barrier) || !Barrier->CanBlockRoom(RoomTag)
					|| !Barrier->OverlapsJoinCapsule(Capsule->GetComponentLocation(),
						Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight())) continue;
				// 요청 방에 직접 연결되면 그 방을 사용한다. 형제 외곽은 명시된 연결 방을
				// 사용하며, 거리나 Barrier 로컬 축으로 '안쪽'을 추측하지 않는다.
				if (Barrier->ServesRoom(RoomTag)) DestinationRoom = Runtime->RoomVolume.Get();
				else
				{
					TArray<FGameplayTag> BoundRooms;
					Barrier->GetSafetyRoomTags(BoundRooms);
					for (FGameplayTag Tag : BoundRooms)
						if (URoomTagComponent::AreRoomsInSameGroup(Tag, RoomTag))
							if (ARoomVolume* Room = GetRegisteredRoomVolume(Tag)) { DestinationRoom = Room; break; }
					// 그룹 자체는 RoomVolume이 아닐 수 있다. 이 경우 준비 요청의 실제 방을 사용한다.
					if (!DestinationRoom) DestinationRoom = Runtime->RoomVolume.Get();
				}
				if (DestinationRoom) break;
			}
			if (DestinationRoom)
			{
				Work.Players.Add(Player);
				Work.Rooms.Add(DestinationRoom);
				Work.Destinations.Add(Player->GetActorLocation());
				Work.Crouched.Add(Player->bIsCrouched);
				Work.CandidateQueries.Add(FGuid());
				const TWeakObjectPtr<const AOutlierPlayerState> WeakPair(Pair);
				const TWeakObjectPtr<AFirstPersonCharacter> WeakPlayer(Player);
				const TWeakObjectPtr<AController> Controller(Player->GetController());
				const TWeakObjectPtr<APlayerState> PlayerStateIdentity(Player->GetPlayerState());
				const int32 PairId = Pair->GetPairId();
				const bool bSuitAcquired = Pair->IsPairSuitAcquired();
				TFunction<bool()> PreviousCheck = MoveTemp(Work.IsCurrent);
				Work.IsCurrent = [PreviousCheck = MoveTemp(PreviousCheck), WeakPair, WeakPlayer,
					Controller, PlayerStateIdentity, PairId, bSuitAcquired]()
				{
					const AOutlierPlayerState* CurrentPair = WeakPair.Get();
					const AFirstPersonCharacter* CurrentPlayer = WeakPlayer.Get();
					return PreviousCheck() && CurrentPair && CurrentPlayer
						&& CurrentPair->GetPairId() == PairId && CurrentPair->IsPairSuitAcquired() == bSuitAcquired
						&& (CurrentPair->GetShooterCharacter() == CurrentPlayer || CurrentPair->GetPartnerCharacter() == CurrentPlayer)
						&& !Controller.IsStale() && !PlayerStateIdentity.IsStale()
						&& CurrentPlayer->GetController() == Controller.Get()
						&& CurrentPlayer->GetPlayerState() == PlayerStateIdentity.Get();
				};
			}
		}
	}
	if (Work.Players.IsEmpty()) return false;
	const FGuid Id = Work.Id;
	BarrierClosurePreparations.Add(RoomTag, MoveTemp(Work));
	AdvanceBarrierClosure(RoomTag, Id);
	return false;
}

bool URoomCombatSubsystem::IsBarrierClosureCurrent(FGameplayTag RoomTag, FGuid Id) const
{
	const FBarrierClosurePreparation* Work = BarrierClosurePreparations.Find(RoomTag);
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!CanRunServerGameplay() || !Work || Work->Id != Id || !Runtime
		|| Runtime->RegistrationId != Work->RegistrationId || Runtime->GameplayGeneration != Work->Generation
		|| !Work->IsCurrent()) return false;
	for (int32 Index = 0; Index < Work->Players.Num(); ++Index)
	{
		const AFirstPersonCharacter* Player = Work->Players[Index].Get();
		const ARoomVolume* Room = Work->Rooms[Index].Get();
		if (!IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->GetController()
			|| !IsValid(Room) || GetRegisteredRoomVolume(Room->GetRoomTag()) != Room
			|| Player->IsSuitTransitionBlocked()) return false;
		const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Player);
		const APartnerCharacter* Partner = Cast<APartnerCharacter>(Player);
		const UOutlierAbilitySystemComponent* ASC = Shooter ? Shooter->GetOutlierAbilitySystemComponent()
			: Partner ? Partner->GetOutlierAbilitySystemComponent() : nullptr;
		if (ASC && (ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
			|| ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))) return false;
	}
	return true;
}

bool URoomCombatSubsystem::IsBarrierDestinationSafe(FGameplayTag RoomTag, FGuid Id, int32 PlayerIndex,
	const FVector& Location, float Radius, float HalfHeight) const
{
	if (!IsBarrierClosureCurrent(RoomTag, Id)) return false;
	const FBarrierClosurePreparation& Work = BarrierClosurePreparations.FindChecked(RoomTag);
	if (!IsCombatJoinCapsuleSafe(Work.Rooms[PlayerIndex].Get(), Location, Radius, HalfHeight)) return false;
	const AFirstPersonCharacter* Moving = Work.Players[PlayerIndex].Get();
	const UCapsuleComponent* Capsule = Moving->GetCapsuleComponent();
	// 먼저 선택한 다른 플레이어의 예약 위치까지 검사한다. Pawn Ignore 응답도 포개짐을 허용하지 않는다.
	const AGameStateBase* State = GetWorld()->GetGameState();
	for (APlayerState* PlayerState : State->PlayerArray)
	{
		const AOutlierPlayerState* Pair = Cast<AOutlierPlayerState>(PlayerState);
		if (!Pair) continue;
		for (const AFirstPersonCharacter* Other : {
			static_cast<const AFirstPersonCharacter*>(Pair->GetShooterCharacter()),
			static_cast<const AFirstPersonCharacter*>(Pair->GetPartnerCharacter())})
		{
			if (!IsValid(Other) || Other == Moving) continue;
			const UCapsuleComponent* OtherCapsule = Other->GetCapsuleComponent();
			if (!OtherCapsule) continue;
			const int32 OtherIndex = Work.Players.IndexOfByKey(Other);
			const FVector OtherLocation = OtherIndex >= 0
				&& (OtherIndex < PlayerIndex || Work.NextPlayer >= Work.Players.Num())
				? Work.Destinations[OtherIndex] : Other->GetActorLocation();
			if (FVector::DistSquared2D(Location, OtherLocation) < FMath::Square(Radius + OtherCapsule->GetScaledCapsuleRadius())
				&& FMath::Abs(Location.Z - OtherLocation.Z) < HalfHeight + OtherCapsule->GetScaledCapsuleHalfHeight()) return false;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BarrierPush), false, Moving);
	for (const auto& Entry : RegisteredBarriers)
		if (Entry.IsValid() && Entry->CanBlockRoom(RoomTag)) Params.AddIgnoredActor(Entry.Get());
	// 목적지가 비었더라도 벽/기둥을 통과해 안쪽으로 보내지 않는다.
	return !GetWorld()->SweepTestByChannel(Moving->GetActorLocation(), Location, Capsule->GetComponentQuat(),
		Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), Params,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
}

void URoomCombatSubsystem::AdvanceBarrierClosure(FGameplayTag RoomTag, FGuid Id)
{
	// 취소된 EQS의 늦은 완료가 같은 방의 새 준비를 지우지 못하게 시도 ID부터 확인한다.
	const FBarrierClosurePreparation* Pending = BarrierClosurePreparations.Find(RoomTag);
	if (!Pending || Pending->Id != Id) return;
	if (!IsBarrierClosureCurrent(RoomTag, Id)) { FinishBarrierClosure(RoomTag, false); return; }
	FBarrierClosurePreparation Work = *Pending;
	if (Work.NextPlayer >= Work.Players.Num())
	{
		// 첫 후보를 찾은 뒤 다른 플레이어를 기다리는 사이에도 바닥/자세가 바뀔 수 있다.
		// 보존한 각 EQS 결과를 모두 재검사한 뒤 최신 중심/자세로 배치를 시작한다.
		for (int32 Index = 0; Index < Work.Players.Num(); ++Index)
		{
			UPlayerRelocationQuery* Query = BarrierClosureCandidateQueries.FindRef(Work.CandidateQueries[Index]);
			const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Work.Players[Index].Get());
			const UShooterMovementComponent* Movement = Shooter ? Shooter->FindComponentByClass<UShooterMovementComponent>() : nullptr;
			if (!Query || !(Movement ? Query->RevalidateForCrouchIntent(Movement->WantsToCrouch()) : Query->Revalidate()))
			{ FinishBarrierClosure(RoomTag, false); return; }
			const FPlayerRelocationResult Landing = Query->GetResult();
			FBarrierClosurePreparation& Current = BarrierClosurePreparations.FindChecked(RoomTag);
			Current.Destinations[Index] = Landing.Location;
			Current.Crouched[Index] = Landing.Posture == EPlayerRelocationPosture::Crouched;
		}
		Work = BarrierClosurePreparations.FindChecked(RoomTag);
		// 모든 후보를 찾은 뒤에만 실제 배치한다. 한 명이라도 실패하면 닫힘/전투를 확정하지 않는다.
		for (int32 Index = 0; Index < Work.Players.Num(); ++Index)
		{
			if (!IsBarrierClosureCurrent(RoomTag, Id)) { FinishBarrierClosure(RoomTag, false); return; }
			AFirstPersonCharacter* Player = Work.Players[Index].Get();
			const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
			float Height = Capsule->GetScaledCapsuleHalfHeight();
			if (Work.Crouched[Index]) Height = FMath::Max(Capsule->GetScaledCapsuleRadius(),
				Player->GetCharacterMovement()->GetCrouchedHalfHeight() * Capsule->GetShapeScale());
			UPlayerRelocationQuery* Query = BarrierClosureCandidateQueries.FindRef(Work.CandidateQueries[Index]);
			if (!Query || !Query->Revalidate()
				|| !IsBarrierDestinationSafe(RoomTag, Id, Index, Work.Destinations[Index], Capsule->GetScaledCapsuleRadius(), Height))
			{ FinishBarrierClosure(RoomTag, false); return; }
			AShooterCharacter* Shooter = Cast<AShooterCharacter>(Player);
			UShooterCharacterMovementComponent* Movement = Shooter
				? Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement()) : nullptr;
			const bool bMoved = Shooter ? Movement && Movement->TeleportToRelocation(Work.Destinations[Index], Work.Crouched[Index])
				: Player->SetActorLocation(Work.Destinations[Index], true);
			if (!bMoved || !IsBarrierClosureCurrent(RoomTag, Id)
				|| !Player->GetActorLocation().Equals(Work.Destinations[Index], 0.1f))
			{ FinishBarrierClosure(RoomTag, false); return; }
			Player->GetCapsuleComponent()->UpdateOverlaps();
			Player->ForceNetUpdate();
		}
		FinishBarrierClosure(RoomTag, !HasBarrierOccupants(RoomTag));
		return;
	}
	const int32 Index = Work.NextPlayer;
	AFirstPersonCharacter* Player = Work.Players[Index].Get();
	ARoomVolume* Room = Work.Rooms[Index].Get();
	FPlayerRelocationRequest Request;
	Request.MovingCharacter = Player;
	Request.Anchor = Room;
	Request.Reference = Player->GetActorTransform();
	Request.Mode = Cast<AShooterCharacter>(Player) ? EPlayerRelocationMode::Ground : EPlayerRelocationMode::Flight;
	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Player))
		if (const auto* Movement = Shooter->FindComponentByClass<UShooterMovementComponent>())
			Request.bWantsCrouch = Movement->WantsToCrouch();
	const TWeakObjectPtr<URoomCombatSubsystem> WeakThis(this);
	Request.IsContextCurrent = [WeakThis, RoomTag, Id]
	{ return WeakThis.IsValid() && WeakThis->IsBarrierClosureCurrent(RoomTag, Id); };
	Request.AdditionalFilter = [WeakThis, RoomTag, Id, Index](const FVector& Location, float Radius, float Height)
	{ return WeakThis.IsValid() && WeakThis->IsBarrierDestinationSafe(RoomTag, Id, Index, Location, Radius, Height); };
	if (Work.bNearbyCandidates)
	{
		// 가장 가까운 Room 내부 보정점부터 시도한다. 이 작은 국소 분리가 실패할 때만
		// 공통 EQS의 주변 격자/자세 단계를 사용하며, 임의의 먼 출입구로 보내지 않는다.
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		FVector Inside;
		const FVector Extent(Capsule->GetScaledCapsuleRadius() + 3, Capsule->GetScaledCapsuleRadius() + 3,
			Capsule->GetScaledCapsuleHalfHeight() + 3);
		if (Room->TryClampFlightLocation(Player->GetActorLocation(), Extent, Inside))
		{
			const FVector Direction = (Room->GetActorLocation() - Inside).GetSafeNormal2D();
			for (float Distance : {0.0f, 10.0f, 25.0f, 50.0f, 100.0f})
				Request.ExplicitCandidates.Add(Inside + Direction * Distance);
		}
		if (Request.ExplicitCandidates.IsEmpty())
		{
			BarrierClosurePreparations.FindChecked(RoomTag).bNearbyCandidates = false;
			AdvanceBarrierClosure(RoomTag, Id);
			return;
		}
	}
	UPlayerRelocationQuery* Query = NewObject<UPlayerRelocationQuery>(this);
	BarrierClosureQueries.Add(RoomTag, Query);
	Query->Start(Request, [WeakThis, RoomTag, Id](const FPlayerRelocationResult& Result)
	{
		if (!WeakThis.IsValid()) return;
		const FBarrierClosurePreparation* PendingWork = WeakThis->BarrierClosurePreparations.Find(RoomTag);
		if (!PendingWork || PendingWork->Id != Id) return;
		if (!WeakThis->IsBarrierClosureCurrent(RoomTag, Id))
		{ WeakThis->FinishBarrierClosure(RoomTag, false); return; }
		FBarrierClosurePreparation& Current = WeakThis->BarrierClosurePreparations.FindChecked(RoomTag);
		UPlayerRelocationQuery* Completed = WeakThis->BarrierClosureQueries.FindRef(RoomTag);
		if (Result.Status != EPlayerRelocationStatus::Success || !Completed || !Completed->Revalidate())
		{
			if (Current.bNearbyCandidates && Result.Status == EPlayerRelocationStatus::NoCandidate)
			{
				Current.bNearbyCandidates = false;
				WeakThis->AdvanceBarrierClosure(RoomTag, Id);
			}
			else WeakThis->FinishBarrierClosure(RoomTag, false);
			return;
		}
		const FPlayerRelocationResult Landing = Completed->GetResult();
		Current.Destinations[Current.NextPlayer] = Landing.Location;
		Current.Crouched[Current.NextPlayer] = Landing.Posture == EPlayerRelocationPosture::Crouched;
		const FGuid CandidateId = FGuid::NewGuid();
		Current.CandidateQueries[Current.NextPlayer] = CandidateId;
		WeakThis->BarrierClosureCandidateQueries.Add(CandidateId, Completed);
		++Current.NextPlayer;
		Current.bNearbyCandidates = true;
		WeakThis->AdvanceBarrierClosure(RoomTag, Id);
	});
}

void URoomCombatSubsystem::FinishBarrierClosure(FGameplayTag RoomTag, bool bSafe)
{
	const FBarrierClosurePreparation* Pending = BarrierClosurePreparations.Find(RoomTag);
	if (!Pending) return;
	// 마지막 Overlap 통지가 전투 차수/세대를 바꿨을 수도 있으므로 완료 전달 직전에 다시 확인한다.
	bSafe = bSafe && IsBarrierClosureCurrent(RoomTag, Pending->Id);
	FBarrierClosurePreparation Work;
	if (!BarrierClosurePreparations.RemoveAndCopyValue(RoomTag, Work)) return;
	TObjectPtr<UPlayerRelocationQuery> Query;
	BarrierClosureQueries.RemoveAndCopyValue(RoomTag, Query);
	if (Query) Query->Cancel();
	for (const FGuid CandidateId : Work.CandidateQueries)
	{
		TObjectPtr<UPlayerRelocationQuery> Candidate;
		if (BarrierClosureCandidateQueries.RemoveAndCopyValue(CandidateId, Candidate) && Candidate) Candidate->Cancel();
	}
	UE_LOG(LogTemp, Display, TEXT("[RoomCombatBarrier] Safety preparation %s Room=%s Generation=%d Players=%d"),
		bSafe ? TEXT("ready") : TEXT("deferred"), *RoomTag.ToString(), Work.Generation, Work.Players.Num());
	if (Work.Completion) Work.Completion(bSafe);
	// 트리거 요청자는 비동기 준비 중 false를 받고 readiness를 기다린다.
	// 완료 콜백에서 리로드/재등록이 발생했다면 이전 준비의 신호는 내보내지 않는다.
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (bSafe && Runtime && Runtime->RegistrationId == Work.RegistrationId
		&& Runtime->GameplayGeneration == Work.Generation)
	{
		// 늦게 등록된 다른 차단막이 앞선 준비를 취소했을 수 있다. 마지막 요청자만
		// 닫으면 먼저 보류된 출입구가 열린 채 남으므로 같은 확정 집합을 함께 갱신한다.
		const TArray<TWeakObjectPtr<ARoomCombatBarrier>> Barriers = RegisteredBarriers;
		for (const TWeakObjectPtr<ARoomCombatBarrier>& Entry : Barriers)
		{
			Runtime = RoomRuntimes.Find(RoomTag);
			if (!Runtime || Runtime->RegistrationId != Work.RegistrationId
				|| Runtime->GameplayGeneration != Work.Generation) return;
			if (ARoomCombatBarrier* Barrier = Entry.Get(); IsValid(Barrier)
				&& RegisteredBarriers.Contains(Barrier) && Barrier->CanBlockRoom(RoomTag))
			{
				Barrier->RefreshFromRoomCombat();
			}
		}
		Runtime = RoomRuntimes.Find(RoomTag);
		if (!Runtime || Runtime->RegistrationId != Work.RegistrationId
			|| Runtime->GameplayGeneration != Work.Generation) return;
		OnRoomStartReadinessChanged.Broadcast(RoomTag);
	}
}
void URoomCombatSubsystem::RegisterBarrier(ARoomCombatBarrier* Barrier)
{
	if (CanRunServerGameplay() && IsValid(Barrier))
	{
		if (RegisteredBarriers.Contains(Barrier)) return;
		TArray<FGameplayTag> PendingRooms;
		BarrierClosurePreparations.GetKeys(PendingRooms);
		// 무관한 그룹의 WP 등록은 이 방의 후보 집합을 바꾸지 않는다.
		for (FGameplayTag Tag : PendingRooms)
			if (Barrier->CanBlockRoom(Tag)) FinishBarrierClosure(Tag, false);
		RegisteredBarriers.AddUnique(Barrier);
	}
}

void URoomCombatSubsystem::UnregisterBarrier(ARoomCombatBarrier* Barrier)
{
	// 후보를 검사한 차단막 집합이 바뀌면 이전 결과로 닫지 않는다.
	TArray<FGameplayTag> PendingRooms;
	BarrierClosurePreparations.GetKeys(PendingRooms);
	TMap<FGameplayTag, TPair<FGuid, int32>> ActiveRoomsToRefresh;
	for (FGameplayTag Tag : PendingRooms)
	{
		// EndPlay에서는 이미 파괴 표시가 설정될 수 있다. 호출 중인 Actor의 태그는
		// 아직 읽을 수 있으므로 IsValid로 자신의 등록 해제를 건너뛰지 않는다.
		if (!Barrier || !Barrier->CanBlockRoom(Tag)) continue;
		const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Tag);
		if (Runtime && Runtime->State == ERoomCombatState::Combat)
			ActiveRoomsToRefresh.Add(Tag, TPair<FGuid, int32>(Runtime->RegistrationId, Runtime->GameplayGeneration));
		FinishBarrierClosure(Tag, false);
	}
	RegisteredBarriers.RemoveAllSwap([Barrier](const TWeakObjectPtr<ARoomCombatBarrier>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Barrier;
	});
	// 진행 중 전투의 출입구만 변경된 집합으로 다시 준비한다. 초기 감지 취소를
	// 재시작하지 않으며, World 종료/Reset 중 EndPlay는 새 쿼리를 만들지 않는다.
	if (!CanRunServerGameplay() || GetWorld()->bIsTearingDown) return;
	const TArray<TWeakObjectPtr<ARoomCombatBarrier>> RemainingBarriers = RegisteredBarriers;
	for (const auto& Room : ActiveRoomsToRefresh)
	{
		for (const TWeakObjectPtr<ARoomCombatBarrier>& Entry : RemainingBarriers)
		{
			const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Room.Key);
			if (!Runtime || Runtime->RegistrationId != Room.Value.Key || Runtime->GameplayGeneration != Room.Value.Value
				|| Runtime->State != ERoomCombatState::Combat) break;
			if (ARoomCombatBarrier* Remaining = Entry.Get(); IsValid(Remaining)
				&& RegisteredBarriers.Contains(Remaining) && Remaining->CanBlockRoom(Room.Key))
			{
				Remaining->RefreshFromRoomCombat();
			}
		}
	}
}

void URoomCombatSubsystem::BroadcastCombatEvent(
	FGameplayTag RoomTag, ERoomCombatEvent Event, int32 PhaseIndex, int32 Generation,
	FGuid RegistrationId, FGuid ExecutionId)
{
	// 다른 수신자가 Reset/재등록해도 이 알림이 새 운행의 문맥을 얻지 않도록 먼저 복사한다.
	if (!RegistrationId.IsValid())
	{
		if (const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag))
		{
			RegistrationId = Runtime->RegistrationId;
			ExecutionId = Runtime->CheckpointHoldExecutionId;
		}
	}
	const FRoomCombatLifecycleEvent Context{RoomTag, Event, PhaseIndex, Generation, RegistrationId, ExecutionId};
	if (Event == ERoomCombatEvent::RoomCleared || Event == ERoomCombatEvent::Cancelled)
	{
		const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
		if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->GameplayGeneration == Generation)
		{
			// 상태 확정 뒤의 알림 경계에서 정리한다. 효과 제거 콜백이 Reset해도
			// 호출자가 보관한 Runtime 포인터로 후속 상태를 변경하지 않는다.
			ClearForcedJoinProtectionsForRoom(RoomTag);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Event=%d Room=%s Phase=%d Generation=%d"),
		static_cast<int32>(Event), *RoomTag.ToString(), PhaseIndex, Generation);
	PublishCombatMusicState(Event != ERoomCombatEvent::Cancelled);
	OnCombatEvent.Broadcast(RoomTag, Event, PhaseIndex, Generation);
	OnCombatLifecycleEvent.Broadcast(Context);
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

	UGameInstance* GameInstance = World->GetGameInstance();
	UOutlierSaveSubSystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	const FOutlierRoomPhaseProgress* SavedProgress = SaveSubsystem
		? SaveSubsystem->GetCurrentRoomPhaseProgress().Find(RoomTag) : nullptr;
	// 복원 차단을 먼저 켜고 예약만 실패하면 두 그룹의 외곽이 동시에 닫힌다.
	// 다른 그룹의 참여 기록은 등록 자체를 거부해 부분 적용을 막는다.
	if (SavedProgress && SavedProgress->bExitBlockActive
		&& !SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName())
		&& !CanReserveCombatRoom(RoomTag))
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Conflicting restored combat group. Room=%s ActiveGroup=%s"),
			*RoomTag.ToString(), *ActiveCombatGroupTag.ToString());
		return false;
	}
	++MovementRoomRevision;
	FRoomCombatRuntime& Runtime = RoomRuntimes.Add(RoomTag);
	Runtime.RoomVolume = RoomVolume;
	Runtime.RegistrationId = FGuid::NewGuid();
	if (FRoomStartHold* Hold = RoomStartHolds.Find(RoomTag);
		Hold && !Hold->RoomRegistrationId.IsValid() && Hold->Owner.IsValid())
	{
		// 등록 완료 알림과 자동 시작보다 먼저 연결해야 도착 중의 늦은 등록도 같은 예약을 따른다.
		Hold->RoomRegistrationId = Runtime.RegistrationId;
	}
	if (const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>())
	{
		Runtime.GameplayGeneration = static_cast<int32>(Arena->GetGameplayGeneration());
	}

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
	else if (SaveSubsystem)
	{
		if (const FOutlierRoomPhaseProgress* Saved =
			SaveSubsystem->GetCurrentRoomPhaseProgress().Find(RoomTag))
		{
			const FRoomCombatPhaseDefinition* Phase = Definition->FindPhase(Saved->NextPhaseIndex);
			if (Phase && (Saved->NextPhaseIndex > 0
				|| (Saved->NextPhaseIndex == 0 && Saved->bResumeCurrentPhase && Saved->bExitBlockActive)))
			{
				Runtime.CurrentCombatPhaseIndex = Saved->NextPhaseIndex;
				Runtime.bExitBlockActive = Saved->bExitBlockActive;
				Runtime.bTriggeredSequenceActive = Saved->bTriggeredSequenceActive;
				Runtime.ActiveActivationGroupTag = Saved->ActivationGroupTag;
				Runtime.bSavedCurrentPhasePending = Saved->bResumeCurrentPhase && Saved->bExitBlockActive;
				Runtime.bSavedAutomaticPhasePending =
					Phase->StartPolicy == ERoomCombatPhaseStartPolicy::Automatic;
				Runtime.State = (Runtime.bSavedAutomaticPhasePending || Runtime.bSavedCurrentPhasePending)
					? ERoomCombatState::Dormant : ERoomCombatState::WaitingForTrigger;
				if (Runtime.bExitBlockActive)
				{
					if (CanReserveCombatRoom(RoomTag)) ReserveCombatRoom(RoomTag);
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Invalid restored phase Room=%s Phase=%d"),
					*RoomTag.ToString(), Saved->NextPhaseIndex);
			}
		}
		if (Runtime.CurrentCombatPhaseIndex == 0 && !Runtime.bSavedCurrentPhasePending)
		{
			if (const FRoomCombatPhaseDefinition* FirstPhase = Definition->FindPhase(0);
				FirstPhase && (FirstPhase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger
					|| FirstPhase->StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger))
			{
				Runtime.State = ERoomCombatState::WaitingForTrigger;
			}
		}
	}
	else if (const FRoomCombatPhaseDefinition* FirstPhase = Definition->FindPhase(0);
		FirstPhase && (FirstPhase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger
			|| FirstPhase->StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger))
	{
		Runtime.State = ERoomCombatState::WaitingForTrigger;
	}

	// Destroy는 EndPlay → UnregisterEnemy로 재진입해 Pending 집합을 변경한다.
	// Room 등록이 처리할 목록을 먼저 분리해야 복원 중 선행 적을 지워도 순회가 무효화되지 않는다.
	TSet<TWeakObjectPtr<AEnemyBase>> PendingEnemies;
	if (PendingPreplacedEnemies.RemoveAndCopyValue(RoomTag, PendingEnemies))
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : PendingEnemies)
		{
			AEnemyBase* Enemy = EnemyPtr.Get();
			if (!IsValid(Enemy))
			{
				continue;
			}

			if (Runtime.State == ERoomCombatState::Cleared
				|| Runtime.CurrentCombatPhaseIndex > 0)
			{
				Enemy->Destroy();
				RegisteredEnemies.Remove(EnemyPtr);
				continue;
			}

			Runtime.TrackedAliveEnemies.Add(EnemyPtr);
			Runtime.bHadPreplacedEnemy = true;
		}
	}
	// SpawnPoint가 RoomVolume보다 먼저 등록됐어도 복원된 완료 상태를 적용한다.
	ApplyRoomClearedToSpawnPoints(RoomTag, Runtime.State == ERoomCombatState::Cleared);
	// 자동 복원은 양쪽 준비 완료 후에 대기를 시작한다. 외부 차수는 등록 후 트리거를 기다린다.
	if (Runtime.CurrentCombatPhaseIndex > 0 && Runtime.State == ERoomCombatState::WaitingForTrigger)
	{
		const FGuid RegistrationId = Runtime.RegistrationId;
		const int32 Generation = Runtime.GameplayGeneration;
		const int32 PhaseIndex = Runtime.CurrentCombatPhaseIndex;
		BeginPhaseDelay(RoomTag, Runtime);
		if (GetPhaseDelayRemainingSeconds(RoomTag) == 0.0f)
		{
			CompletePhaseDelay(RoomTag, RegistrationId, Generation, PhaseIndex);
		}
	}
	OnRoomStartReadinessChanged.Broadcast(RoomTag);
	// 복원은 시작 이벤트를 재생하지 않는다. 이미 로드된 외곽 차단막도 현재 값으로 맞춘다.
	for (const TWeakObjectPtr<ARoomCombatBarrier>& Barrier : RegisteredBarriers)
	{
		if (Barrier.IsValid() && Barrier->CanBlockRoom(RoomTag)) Barrier->RefreshFromRoomCombat();
	}
	if (bRestoredAutomaticStartAllowed)
	{
		TryStartRestoredAutomaticPhase(RoomTag);
	}

	return true;
}

bool URoomCombatSubsystem::HasReadyExternalTriggerRoster(
	const ARoomVolume* RoomVolume, FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* Phase = Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex) : nullptr;
	const FRoomCombatWaveDefinition* FirstWave = Definition && Runtime
		? Definition->FindWave(Runtime->CurrentCombatPhaseIndex, 0) : nullptr;
	if (!CanRunServerGameplay() || !IsValid(RoomVolume)
		|| !Runtime || Runtime->RoomVolume.Get() != RoomVolume
		|| Runtime->State != ERoomCombatState::WaitingForTrigger
		|| Runtime->bPhaseDelayPending
		|| !CanReserveCombatRoom(RoomTag)
		|| !Phase || Phase->StartPolicy != ERoomCombatPhaseStartPolicy::ExternalTrigger
		|| !FirstWave || !FirstWave->IsSpawnFromObjects()
		|| Phase->ExpectedStartSpawnPointCount <= 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] External start not ready. Room=%s RequestedVolume=%s RegisteredVolume=%s State=%d ActiveRoom=%s Phase=%d Policy=%d FirstWaveSpawnFromObjects=%d ExpectedPoints=%d"),
			*RoomTag.ToString(), *GetNameSafe(RoomVolume),
			Runtime ? *GetNameSafe(Runtime->RoomVolume.Get()) : TEXT("None"),
			Runtime ? static_cast<int32>(Runtime->State) : -1,
			*ActiveCombatRoomTag.ToString(), Runtime ? Runtime->CurrentCombatPhaseIndex : -1,
			Phase ? static_cast<int32>(Phase->StartPolicy) : -1,
			FirstWave && FirstWave->IsSpawnFromObjects(),
			Phase ? Phase->ExpectedStartSpawnPointCount : 0);
		return false;
	}

	int32 ReadyCount = 0;
	int32 RegisteredCount = 0;
	int32 InactiveCount = 0;
	int32 TagMismatchCount = 0;
	if (const TArray<FRoomCombatSpawnPointRuntime>* Points = SpawnPointsByRoom.Find(RoomTag))
	{
		for (const FRoomCombatSpawnPointRuntime& Entry : *Points)
		{
			++RegisteredCount;
			const ARoomCombatSpawnPoint* Point = Entry.SpawnPoint.Get();
			if (!IsValid(Point) || !Point->IsRuntimeActive())
			{
				++InactiveCount;
			}
			else if (FirstWave->RequiredSpawnPointTag.IsValid()
				&& !Entry.SpawnPointTags.HasTag(FirstWave->RequiredSpawnPointTag))
			{
				++TagMismatchCount;
			}
			else
			{
				++ReadyCount;
			}
		}
	}
	if (ReadyCount < Phase->ExpectedStartSpawnPointCount)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] External start roster waiting. Room=%s Ready=%d Expected=%d Registered=%d InactiveOrUnloaded=%d TagMismatch=%d RequiredTag=%s"),
			*RoomTag.ToString(), ReadyCount, Phase->ExpectedStartSpawnPointCount,
			RegisteredCount, InactiveCount, TagMismatchCount,
			*FirstWave->RequiredSpawnPointTag.ToString());
	}
	return ReadyCount >= Phase->ExpectedStartSpawnPointCount;
}

void URoomCombatSubsystem::UnregisterRoom(ARoomVolume* RoomVolume)
{
	if (!RoomVolume)
	{
		return;
	}

	FGameplayTag RoomTag = RoomVolume->GetRoomTag();
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || Runtime->RoomVolume.Get() != RoomVolume)
	{
		// RegisterRoom은 명시적인 RoomTag를 받으므로 테스트나 동적 구성에서는
		// Actor의 편집 프로퍼티보다 등록 당시의 포인터 매핑이 실제 소유권이다.
		RoomTag = FGameplayTag();
		for (const TPair<FGameplayTag, FRoomCombatRuntime>& Entry : RoomRuntimes)
		{
			if (Entry.Value.RoomVolume.Get() == RoomVolume)
			{
				RoomTag = Entry.Key;
				break;
			}
		}
		if (!RoomTag.IsValid())
		{
			return;
		}

		Runtime = RoomRuntimes.Find(RoomTag);
	}
	++MovementRoomRevision;
	const bool bWasSequenceActive = Runtime->bExitBlockActive
		|| Runtime->bTriggeredSequenceActive
		|| Runtime->CheckpointHoldExecutionId.IsValid()
		|| Runtime->State == ERoomCombatState::Preparing;
	const int32 CancelledPhase = Runtime->CurrentCombatPhaseIndex;
	const int32 CancelledGeneration = Runtime->GameplayGeneration;
	const FGuid CancelledRegistrationId = Runtime->RegistrationId;
	const FGuid CancelledExecutionId = Runtime->CheckpointHoldExecutionId;
	// 목적지 방은 전투 소속 방과 다를 수 있다. 어느 쪽이 언로드돼도 합류 보호를 끝낸다.
	TArray<FGameplayTag> PendingBarrierRooms;
	BarrierClosurePreparations.GetKeys(PendingBarrierRooms);
	for (FGameplayTag Tag : PendingBarrierRooms) FinishBarrierClosure(Tag, false);
	CancelInitialDetectionJoinsForRoom(RoomTag);
	ClearForcedJoinProtectionsForRoom(RoomTag);
	Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || Runtime->RegistrationId != CancelledRegistrationId)
	{
		return;
	}
	RoomStartHolds.Remove(RoomTag);
	GetWorld()->GetTimerManager().ClearTimer(Runtime->InitialSpawnDelayTimer);
	GetWorld()->GetTimerManager().ClearTimer(Runtime->PhaseDelayTimer);
	SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, false);
	ResetWaveTurretsForRoom(RoomTag, TEXT("Room combat unregistered"));

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

	ReleaseCombatRoom(RoomTag);
	if (UWorld* World = GetWorld())
	{
		if (UEnemyRoomSubsystem* EnemyRoomSubsystem =
			World->GetSubsystem<UEnemyRoomSubsystem>())
		{
			EnemyRoomSubsystem->NotifyRoomCombatEnded(RoomTag);
		}
	}
	TArray<AEnemyBase*> PooledEnemiesToReturn;
	// 아군화로 생존 집계에서 빠진 개체도 방 소유권 기준으로 정리한다.
	for (const auto& Entry : RegisteredEnemies)
	{
		const TWeakObjectPtr<AEnemyBase> EnemyPtr = Entry.Key;
		const FRoomCombatEnemyRegistration* Registration = &Entry.Value;
		if (!EnemyPtr.IsValid() || Registration->RoomTag != RoomTag)
		{
			continue;
		}

		if (!Registration->bPoolManaged)
		{
			if (Registration->bPreplaced && !Registration->bExcludedAsAlly)
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
	RefreshSpawnPointWorkStates(RoomTag, false);
	RoomRuntimes.Remove(RoomTag);
	PublishCombatMusicState(false);

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
		BroadcastCombatEvent(RoomTag, ERoomCombatEvent::Cancelled, CancelledPhase, CancelledGeneration,
			CancelledRegistrationId, CancelledExecutionId);
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
		// 중복 등록이 같은 Actor를 후보 배열에 두 번 넣지 않게 한다.
		return true;
	}

	FRoomCombatSpawnPointRuntime& Runtime = SpawnPointsByRoom.FindOrAdd(RoomTag).AddDefaulted_GetRef();
	Runtime.SpawnPoint = SpawnPointPtr;
	Runtime.SpawnPointTags = SpawnPointTags;
	Runtime.ActivationGroupTag = ActivationGroupTag;
	RegisteredSpawnPointRooms.Add(SpawnPointPtr, RoomTag);
	if (const FRoomCombatRuntime* Room = RoomRuntimes.Find(RoomTag))
	{
		SpawnPoint->SetRoomCleared(Room->State == ERoomCombatState::Cleared);
		// 그룹을 켠 뒤 등록된 지점도 같은 연속 전투에 참여한다.
		if (ActivationGroupTag.IsValid() && Room->ActiveActivationGroupTag == ActivationGroupTag)
		{
			SpawnPoint->SetRuntimeActive(Room->bTriggeredSequenceActive);
		}
	}
	else if (UWorld* World = GetWorld())
	{
		// Room보다 먼저 로드된 SpawnPoint도 첫 복제부터 완료 외형을 보낸다.
		UGameInstance* GameInstance = World->GetGameInstance();
		const UOutlierSaveSubSystem* SaveSubsystem = GameInstance
			? GameInstance->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
		SpawnPoint->SetRoomCleared(SaveSubsystem && SaveSubsystem->HasWorldProgress(
			EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName()));
	}
	OnRoomStartReadinessChanged.Broadcast(RoomTag);
	if (bRestoredAutomaticStartAllowed)
	{
		TryStartRestoredAutomaticPhase(RoomTag);
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
	if (const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
		Arena && Turret->GetCombatGameplayGeneration() != static_cast<int32>(Arena->GetGameplayGeneration()))
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
	// 같은 Actor가 재등록되어도 완전히 같은 설정은 멱등 성공으로 본다.
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
		if (!Existing || *ExistingRoomTag != RoomTag
			|| Existing->CombatPhaseIndex != CombatPhaseIndex
			|| Existing->WaveIndex != WaveIndex)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomCombat] Wave turret was registered with different settings. Turret=%s ExistingRoom=%s NewRoom=%s"),
				*GetNameSafe(Turret), *ExistingRoomTag->ToString(), *RoomTag.ToString());
			return false;
		}
	}

	// 강제 정리는 처치 기록을 남기지 않는다. 복원/늦은 등록에서도 완료된 선행 차수의 터렛을 되살리지 않는다.
	const FRoomCombatPhaseDefinition* TurretPhase = Definition->FindPhase(CombatPhaseIndex);
	const FRoomCombatPhaseDefinition* FollowingPhase = Definition->FindPhase(CombatPhaseIndex + 1);
	const FRoomCombatRuntime* ProgressRuntime = RoomRuntimes.Find(RoomTag);
	const UOutlierSaveSubSystem* Saves = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	const FOutlierRoomPhaseProgress* SavedProgress = Saves
		? Saves->GetCurrentRoomPhaseProgress().Find(RoomTag) : nullptr;
	// 전체 Encounter 완료는 선행 Phase 정리와 다르다. Room 등록 전에도 남은 모든 터렛을 막는다.
	if ((ProgressRuntime && ProgressRuntime->State == ERoomCombatState::Cleared)
		|| (Saves && Saves->HasWorldProgress(EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName())))
	{
		UnregisterWaveTurret(Turret);
		Turret->Destroy();
		return false;
	}
	const int32 ProgressPhaseIndex = ProgressRuntime ? ProgressRuntime->CurrentCombatPhaseIndex
		: SavedProgress ? SavedProgress->NextPhaseIndex : INDEX_NONE;
	if (TurretPhase && TurretPhase->bReleaseCombatOnCompletion
		&& TurretPhase->StartPolicy == ERoomCombatPhaseStartPolicy::InitialDetection
		&& FollowingPhase && FollowingPhase->StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger
		&& (ProgressPhaseIndex > CombatPhaseIndex || (ProgressRuntime
			&& ProgressRuntime->bPhaseCleanupInProgress && ProgressPhaseIndex == CombatPhaseIndex)))
	{
		UnregisterWaveTurret(Turret);
		Turret->Destroy();
		return false;
	}
	if (RegisteredWaveTurretRooms.Contains(TurretPtr)) return true;

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

	// Gameplay 서브레벨 Actor의 등록이 Wave 시작보다 늦을 수 있다. 현재 Wave가 이 터렛을
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
		// 전개 중 Actor가 해제되어도 완료로 세지 않는다. Pending 수량은 그대로 두고
		// 같은 Wave의 교체 Actor가 등록될 때 다시 전개를 요청한다.
		Runtime->PendingWaveTurretActivations.Remove(TurretPtr);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret unregistered. Turret=%s Room=%s"),
		*GetNameSafe(Turret), *RegisteredRoomTag.ToString());
}

bool URoomCombatSubsystem::NotifyWaveTurretDeploymentFinished(
	AAutoTurret* Turret,
	int32 GameplayGeneration,
	int32 ActivationSerial)
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
		|| Runtime->GameplayGeneration != GameplayGeneration
		|| Runtime->CurrentCombatPhaseIndex != Turret->GetCombatPhaseIndex()
		|| Runtime->CurrentWaveIndex != Turret->GetWaveIndex()
		|| !Runtime->PendingWaveTurretActivations.Contains(TurretPtr)
		|| !Turret->MatchesRoomWaveLifecycle(GameplayGeneration, ActivationSerial))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomCombat] Wave turret completion rejected. Turret=%s Room=%s SuppliedGeneration=%d RuntimeGeneration=%d SuppliedSerial=%d CurrentSerial=%d State=%d Pending=%d Alive=%d"),
			*GetNameSafe(Turret), RegisteredRoomTag ? *RegisteredRoomTag->ToString() : TEXT("None"),
			GameplayGeneration, Runtime ? Runtime->GameplayGeneration : INDEX_NONE,
			ActivationSerial, Turret->GetRoomWaveActivationSerial(),
			static_cast<int32>(Turret->GetTurretLifecycleState()),
			Runtime ? Runtime->PendingActivationCount : 0,
			Runtime ? Runtime->TrackedAliveEnemies.Num() : 0);
		return false;
	}

	if (!RegisterActivatedWaveTurret(*RegisteredRoomTag, *Runtime, Turret))
	{
		return false;
	}

	Runtime->PendingWaveTurretActivations.Remove(TurretPtr);
	Runtime->PendingActivationCount = FMath::Max(Runtime->PendingActivationCount - 1, 0);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Wave turret activated. Turret=%s Room=%s Phase=%d Wave=%d Generation=%d Serial=%d PendingActivation=%d Alive=%d"),
		*GetNameSafe(Turret),
		*RegisteredRoomTag->ToString(),
		Runtime->CurrentCombatPhaseIndex,
		Runtime->CurrentWaveIndex,
		GameplayGeneration,
		ActivationSerial,
		Runtime->PendingActivationCount,
		Runtime->TrackedAliveEnemies.Num());
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
	// 그룹 전투 중에도 요청한 자식 Room에 등록된 지점만 후보로 제공한다.
	if (!RoomTag.IsValid() || !IsRoomCombatReserved(RoomTag))
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
	if (const UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
		Arena && Enemy->GetCombatGameplayGeneration() != static_cast<int32>(Arena->GetGameplayGeneration()))
	{
		return;
	}
	const UOutlierSaveSubSystem* Saves = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	if (Saves && Saves->HasWorldProgress(EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName()))
	{
		// RoomVolume보다 먼저 로드되어도 완료된 전투의 적을 Pending 목록에 되살리지 않는다.
		UnregisterEnemy(Enemy);
		Enemy->Destroy();
		return;
	}

	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	FRoomCombatEnemyRegistration& Registration = RegisteredEnemies.FindOrAdd(EnemyPtr);
	if (Registration.bExcludedAsAlly) return;
	Registration.RoomTag = RoomTag;
	Registration.CombatPhaseIndex = 0;
	Registration.WaveIndex = 0;
	Registration.GameplayGeneration = Enemy->GetCombatGameplayGeneration();
	Registration.bPreplaced = true;
	Registration.bPoolManaged = false;

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime)
	{
		PendingPreplacedEnemies.FindOrAdd(RoomTag).Add(EnemyPtr);
		return;
	}

	if (Runtime->State == ERoomCombatState::Cleared
		|| Runtime->CurrentCombatPhaseIndex > 0 || Runtime->bPhaseCleanupInProgress)
	{
		RegisteredEnemies.Remove(EnemyPtr);
		Enemy->Destroy();
		return;
	}

	Runtime->TrackedAliveEnemies.Add(EnemyPtr);
	Runtime->bHadPreplacedEnemy = true;
	if (bRestoredAutomaticStartAllowed)
	{
		TryStartRestoredAutomaticPhase(RoomTag);
	}
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

void URoomCombatSubsystem::NotifyEnemyAllied(AEnemyBase* Enemy)
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || Enemy->IsDead()
		|| Enemy->GetGenericTeamId() != FGenericTeamId(OutlierTeamIds::Player))
	{
		return;
	}
	const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
	FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
	if (!Registration) return;
	const FGameplayTag RoomTag = Registration->RoomTag;
	Registration->bExcludedAsAlly = true;
	// 아군화는 처치가 아니다. 후속 사망과 풀 반환을 위해 원래 등록 정보는 유지한다.
	if (TSet<TWeakObjectPtr<AEnemyBase>>* Pending = PendingPreplacedEnemies.Find(RoomTag))
	{
		Pending->Remove(EnemyPtr);
	}
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || Runtime->TrackedAliveEnemies.Remove(EnemyPtr) == 0) return;
	if (Runtime->State == ERoomCombatState::Dormant)
	{
		// 발각 전 마지막 배치 적을 빙의한 경우에도 현재 차수의 완료를 판정한다.
		if (Registration->bPreplaced && Runtime->bHadPreplacedEnemy && !Runtime->bInitialDetectionPendingJoin
			&& !Runtime->bSavedCurrentPhasePending && Runtime->TrackedAliveEnemies.IsEmpty())
		{
			CompleteCurrentPhase(RoomTag, true);
		}
		return;
	}
	if (Runtime->State == ERoomCombatState::Combat)
	{
		EvaluateWaveProgress(RoomTag, *Runtime);
	}
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

	// 아군화 때 이미 생존 집계에서 제외됐으면 후속 사망은 등록 정보만 정리한다.
	if (Runtime->TrackedAliveEnemies.Remove(EnemyPtr) == 0) return;
	CompactAliveEnemies(*Runtime);
	if (Runtime->State == ERoomCombatState::Preparing)
	{
		// 발각된 뒤에는 전멸해도 합류 완료까지 기다린 뒤 후속 Wave를 진행한다.
		return;
	}
	if (Runtime->State == ERoomCombatState::Dormant)
	{
		if (bWasPreplaced && Runtime->bHadPreplacedEnemy && !Runtime->bInitialDetectionPendingJoin
			&& !Runtime->bSavedCurrentPhasePending
			&& Runtime->TrackedAliveEnemies.IsEmpty())
		{
			// 발각 전 암살로 전멸한 경우에만 후속 Wave 없이 현재 차수를 끝낸다.
			CompleteCurrentPhase(RoomTag, true);
		}
		return;
	}

	if (Runtime->State == ERoomCombatState::Combat)
	{
		EvaluateWaveProgress(RoomTag, *Runtime);
	}
}

bool URoomCombatSubsystem::IsPlayerInsideRoom(
	const AFirstPersonCharacter* Player, const ARoomVolume* Room) const
{
	// 태그만으로 판단하면 Room 경계나 지연된 Overlap 갱신에서 방 밖 플레이어를 안쪽으로 오인할 수 있다.
	return IsValid(Player) && IsValid(Room)
		&& !Player->IsActorBeingDestroyed()
		&& Player->GetCurrentRoomTag() == Room->GetRoomTag()
		&& Room->ContainsWorldLocation(Player->GetActorLocation());
}

ARoomVolume* URoomCombatSubsystem::FindPlayerRoomInCombatGroup(
	const AFirstPersonCharacter* Player, FGameplayTag CombatRoomTag) const
{
	if (!IsValid(Player) || Player->GetWorld() != GetWorld()) return nullptr;
	const FGameplayTag PlayerRoomTag = Player->GetCurrentRoomTag();
	// 기존 태그는 같은 방만 인정한다. 형제 관계는 정확한 네 단계 태그끼리만 허용한다.
	if (PlayerRoomTag != CombatRoomTag
		&& !URoomTagComponent::AreRoomsInSameGroup(PlayerRoomTag, CombatRoomTag)) return nullptr;
	ARoomVolume* Room = GetRegisteredRoomVolume(PlayerRoomTag);
	return IsPlayerInsideRoom(Player, Room) ? Room : nullptr;
}

bool URoomCombatSubsystem::IsSafeJoinDestination(
	const AFirstPersonCharacter* MovingPlayer, const AFirstPersonCharacter* Anchor,
	const ARoomVolume* Room, const FVector& Location, ERoomJoinRejection* OutRejection) const
{
	if (OutRejection) *OutRejection = ERoomJoinRejection::None;
	auto Reject = [OutRejection](ERoomJoinRejection Reason)
	{
		if (OutRejection) *OutRejection = Reason;
		return false;
	};
	const UCapsuleComponent* Capsule = MovingPlayer ? MovingPlayer->GetCapsuleComponent() : nullptr;
	const UCapsuleComponent* AnchorCapsule = Anchor ? Anchor->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Capsule || !World || !Room)
	{
		return Reject(ERoomJoinRejection::InvalidContext);
	}
	if (!Room->ContainsWorldLocation(Location))
	{
		return Reject(ERoomJoinRejection::CenterOutsideRoom);
	}
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// 목적지 검증 순서: 캡슐 전체의 Room 포함 여부 -> 플레이어/차단막/월드 충돌 -> Shooter 지면.
	// 중심만 방 안인 경계 위치는 캐릭터가 차단막 바깥에 걸칠 수 있다.
	const float Diagonal = Radius * FMath::Sqrt(0.5f);
	for (const FVector& Offset : {FVector(Radius, 0, 0), FVector(-Radius, 0, 0),
		FVector(0, Radius, 0), FVector(0, -Radius, 0),
		FVector(Diagonal, Diagonal, 0), FVector(Diagonal, -Diagonal, 0),
		FVector(-Diagonal, Diagonal, 0), FVector(-Diagonal, -Diagonal, 0),
		FVector(0, 0, HalfHeight), FVector(0, 0, -HalfHeight)})
	{
		if (!Room->ContainsWorldLocation(Location + Offset))
		{
			return Reject(ERoomJoinRejection::CapsuleOutsideRoom);
		}
	}
	if (AnchorCapsule
		&& FMath::Abs(Location.Z - Anchor->GetActorLocation().Z)
			< HalfHeight + AnchorCapsule->GetScaledCapsuleHalfHeight()
		&& FVector::Dist2D(Location, Anchor->GetActorLocation())
			< Radius + AnchorCapsule->GetScaledCapsuleRadius() + 10.0f)
	{
		return Reject(ERoomJoinRejection::AnchorOverlap);
	}
	for (const TWeakObjectPtr<ARoomCombatBarrier>& BarrierPtr : RegisteredBarriers)
	{
		const ARoomCombatBarrier* Barrier = BarrierPtr.Get();
		// 아직 숨겨진 외곽도 합류 직후 닫힐 수 있다. 형제 방 소속의 외곽까지
		// 검사하되, 계속 열려 있는 그룹 내부 통로는 벽으로 취급하지 않는다.
		if (Barrier && Barrier->CanBlockRoom(Room->GetRoomTag())
			&& Barrier->OverlapsJoinCapsule(Location, Radius, HalfHeight))
		{
			return Reject(ERoomJoinRejection::ExitBarrierOverlap);
		}
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RoomCombatJoin), false, MovingPlayer);
	if (Anchor)
	{
		Query.AddIgnoredActor(Anchor);
	}
	if (World->OverlapBlockingTestByProfile(Location, MovingPlayer->GetActorQuat(),
		Capsule->GetCollisionProfileName(), FCollisionShape::MakeCapsule(Radius, HalfHeight), Query))
	{
		return Reject(ERoomJoinRejection::WorldBlockingCollision);
	}
	if (Cast<AShooterCharacter>(MovingPlayer))
	{
		FHitResult GroundHit;
		const FVector Feet = Location - FVector(0, 0, HalfHeight);
		if (!World->LineTraceSingleByChannel(GroundHit, Feet + FVector(0, 0, 30),
			Feet - FVector(0, 0, 70), ECC_Visibility, Query))
		{
			return Reject(ERoomJoinRejection::GroundMissing);
		}
		if (!(GroundHit.ImpactNormal.Z >= 0.7f)) return Reject(ERoomJoinRejection::GroundTooSteep);
	}
	return true;
}

bool URoomCombatSubsystem::FindJoinDestination(
	AFirstPersonCharacter* MovingPlayer, AFirstPersonCharacter* Anchor,
	ARoomVolume* Room, bool bFallbackOnly, FVector& OutLocation, FString* OutRejectionSummary) const
{
	if (OutRejectionSummary) OutRejectionSummary->Reset();
	int32 Rejections[static_cast<int32>(ERoomJoinRejection::Count)] = {};
	auto RecordRejection = [&](ERoomJoinRejection Reason, const FVector& Candidate)
	{
		if (!OutRejectionSummary) return;
		++Rejections[static_cast<int32>(Reason)];
		UE_LOG(LogTemp, Verbose,
			TEXT("[RoomCombat] Join candidate rejected. Method=%s Room=%s Player=%s Reason=%s Location=%s"),
			bFallbackOnly ? TEXT("Fallback") : TEXT("Nearby"),
			*Room->GetRoomTag().ToString(), *GetNameSafe(MovingPlayer), JoinRejectionName(Reason), *Candidate.ToString());
	};
	auto FinishRejectedSearch = [&]()
	{
		if (OutRejectionSummary)
		{
			// 일반 로그는 후보마다 늘리지 않고 실패한 탐색의 원인별 개수를 한 줄로 모은다.
			for (int32 Index = 1; Index < static_cast<int32>(ERoomJoinRejection::Count); ++Index)
			{
				if (Rejections[Index] == 0) continue;
				if (!OutRejectionSummary->IsEmpty()) *OutRejectionSummary += TEXT(",");
				*OutRejectionSummary += FString::Printf(TEXT("%s:%d"),
					JoinRejectionName(static_cast<ERoomJoinRejection>(Index)), Rejections[Index]);
			}
			if (OutRejectionSummary->IsEmpty()) *OutRejectionSummary = TEXT("NoFallbackConfigured");
		}
		return false;
	};
	const UCapsuleComponent* Capsule = MovingPlayer ? MovingPlayer->GetCapsuleComponent() : nullptr;
	const UCapsuleComponent* AnchorCapsule = Anchor ? Anchor->GetCapsuleComponent() : nullptr;
	if (!Capsule || !Room || (!bFallbackOnly && !AnchorCapsule))
	{
		if (OutRejectionSummary) *OutRejectionSummary = TEXT("InvalidContext");
		return false;
	}
	// 재접속 배치는 기존 고정 후보 정책을 유지한다. 최초 발각 합류는 별도의 공통 EQS 요청을 사용한다.
	if (!bFallbackOnly)
	{
		const float Offset = Capsule->GetScaledCapsuleRadius()
			+ AnchorCapsule->GetScaledCapsuleRadius() + 50.0f;
		const FRotator Yaw(0.0f, Anchor->GetActorRotation().Yaw, 0.0f);
		const FVector Forward = Yaw.Vector();
		const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		const FVector Directions[] = {-Forward, -Right, Right, Forward,
			(-Forward - Right).GetSafeNormal(), (-Forward + Right).GetSafeNormal(),
			(Forward - Right).GetSafeNormal(), (Forward + Right).GetSafeNormal()};
		for (const FVector& Direction : Directions)
		{
			FVector Candidate = Anchor->GetActorLocation() + Direction * Offset;
			if (Cast<AShooterCharacter>(MovingPlayer))
			{
				FCollisionQueryParams Query(SCENE_QUERY_STAT(RoomCombatJoinGround), false, MovingPlayer);
				Query.AddIgnoredActor(Anchor);
				FHitResult GroundHit;
				if (!GetWorld()->LineTraceSingleByChannel(GroundHit, Candidate + FVector(0, 0, 100),
					Candidate - FVector(0, 0, 250), ECC_Visibility, Query))
				{
					RecordRejection(ERoomJoinRejection::GroundMissing, Candidate);
					continue;
				}
				Candidate.Z = GroundHit.ImpactPoint.Z + Capsule->GetScaledCapsuleHalfHeight() + 1.0f;
			}
			ERoomJoinRejection Reason;
			if (IsSafeJoinDestination(MovingPlayer, Anchor, Room, Candidate, &Reason))
			{
				OutLocation = Candidate;
				return true;
			}
			RecordRejection(Reason, Candidate);
		}
		return FinishRejectedSearch();
	}

	// 주변 후보는 여기서 끝이다. 두 명 모두 재접속 중이라 기준 플레이어가 없으면
	// 보존한 Pawn의 위치에서 가까운 출입구부터 검사한다.
	const FVector ReferenceLocation = Anchor ? Anchor->GetActorLocation()
		: MovingPlayer->GetActorLocation();
	TArray<TPair<float, ARoomCombatBarrier*>> Fallbacks;
	for (const TWeakObjectPtr<ARoomCombatBarrier>& BarrierPtr : RegisteredBarriers)
	{
		ARoomCombatBarrier* Barrier = BarrierPtr.Get();
		if (Barrier && Barrier->ServesRoom(Room->GetRoomTag()))
		{
			Fallbacks.Emplace(FVector::DistSquared(ReferenceLocation,
				Barrier->GetActorLocation()), Barrier);
		}
	}
	Fallbacks.Sort([](const auto& Left, const auto& Right) { return Left.Key < Right.Key; });
	for (const auto& Entry : Fallbacks)
	{
		const FVector Candidate = Entry.Value->GetJoinFallbackLocation();
		ERoomJoinRejection Reason;
		if (IsSafeJoinDestination(MovingPlayer, Anchor, Room, Candidate, &Reason))
		{
			OutLocation = Candidate;
			return true;
		}
		RecordRejection(Reason, Candidate);
	}
	return FinishRejectedSearch();
}

bool URoomCombatSubsystem::GetReconnectContext(FRoomCombatReconnectContext& OutContext) const
{
	OutContext = FRoomCombatReconnectContext();
	if (!CanRunServerGameplay() || !ActiveCombatGroupId.IsValid())
	{
		return false;
	}
	// 대표 방은 Preparing일 수도 있다. 실제 차단 중인 참여 방을 결정적으로 선택한다.
	for (const auto& Entry : RoomRuntimes)
	{
		const FRoomCombatRuntime& Runtime = Entry.Value;
		if (IsRoomCombatReserved(Entry.Key) && Runtime.bExitBlockActive && Runtime.RoomVolume.IsValid()
			&& (!OutContext.RoomTag.IsValid() || Entry.Key.GetTagName().LexicalLess(OutContext.RoomTag.GetTagName())))
		{
			OutContext.RoomTag = Entry.Key;
			OutContext.RoomRegistrationId = Runtime.RegistrationId;
			OutContext.GameplayGeneration = Runtime.GameplayGeneration;
			OutContext.CombatGroupId = ActiveCombatGroupId;
		}
	}
	return OutContext.RoomTag.IsValid();
}

bool URoomCombatSubsystem::IsReconnectContextCurrent(
	const FRoomCombatReconnectContext& Context) const
{
	FRoomCombatReconnectContext Current;
	return GetReconnectContext(Current)
		&& Context.CombatGroupId.IsValid() && Current.CombatGroupId == Context.CombatGroupId
		&& Current.GameplayGeneration == Context.GameplayGeneration;
}

bool URoomCombatSubsystem::IsReconnectDestinationCurrent(const FRoomCombatReconnectContext& Context) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.DestinationRoomTag);
	return CanRunServerGameplay() && Runtime && Runtime->RoomVolume.IsValid()
		&& Context.DestinationRoomRegistrationId.IsValid()
		&& Runtime->RegistrationId == Context.DestinationRoomRegistrationId
		&& Runtime->GameplayGeneration == Context.GameplayGeneration;
}

bool URoomCombatSubsystem::TryPlaceReconnectingPlayer(
	AFirstPersonCharacter* Player, AFirstPersonCharacter* Anchor,
	FRoomCombatReconnectContext& Context)
{
	if (!IsValid(Player) || Player->GetWorld() != GetWorld()
		|| !Player->HasAuthority() || !IsReconnectContextCurrent(Context))
	{
		return false;
	}
	FRoomCombatReconnectContext Current;
	// 보존 Pawn이 아직 차단막 안쪽의 안전한 자리에 있으면 이동시키지 않는다.
	// Overlap을 먼저 갱신해야 현재 Room 태그와 실제 위치를 함께 판정할 수 있다.
	if (UCapsuleComponent* Capsule = Player->GetCapsuleComponent())
	{
		Capsule->UpdateOverlaps();
	}
	// Overlap 통보가 Reset/언로드를 실행할 수 있다. 위치 유지 성공도 그 이후 수명으로 판정한다.
	if (!IsReconnectContextCurrent(Context) || !GetReconnectContext(Current)) return false;
	ARoomVolume* PlayerRoom = FindPlayerRoomInCombatGroup(Player, Current.RoomTag);
	if (PlayerRoom && IsSafeJoinDestination(Player, nullptr, PlayerRoom, Player->GetActorLocation()))
	{
		Context = Current;
		Context.DestinationRoomTag = PlayerRoom->GetRoomTag();
		Context.DestinationRoomRegistrationId = RoomRuntimes.FindChecked(Context.DestinationRoomTag).RegistrationId;
		return true;
	}
	ARoomVolume* AnchorRoom = FindPlayerRoomInCombatGroup(Anchor, Current.RoomTag);
	if (!AnchorRoom) Anchor = nullptr;
	TArray<FGameplayTag> DestinationRooms;
	if (AnchorRoom) DestinationRooms.Add(AnchorRoom->GetRoomTag());
	else
	{
		for (const auto& Entry : RoomRuntimes)
		{
			if (Entry.Value.bExitBlockActive && IsRoomCombatReserved(Entry.Key)
				&& Entry.Value.RoomVolume.IsValid()) DestinationRooms.Add(Entry.Key);
		}
		DestinationRooms.Sort([](FGameplayTag Left, FGameplayTag Right)
		{
			return Left.GetTagName().LexicalLess(Right.GetTagName());
		});
	}

	// 첫 합류와 같은 안전 검사를 사용한다. 두 플레이어가 모두 끊겼다면
	// Anchor 없이 출입구 fallback만 검사하며, 실패한 위치에 강제로 배치하지 않는다.
	for (FGameplayTag DestinationTag : DestinationRooms)
	{
		const FRoomCombatRuntime* DestinationRuntime = RoomRuntimes.Find(DestinationTag);
		if (!DestinationRuntime || !DestinationRuntime->RoomVolume.IsValid()) continue;
		ARoomVolume* Room = DestinationRuntime->RoomVolume.Get();
		const FGuid DestinationRegistration = DestinationRuntime->RegistrationId;
		auto IsDestinationCurrent = [&]()
		{
			const FRoomCombatRuntime* Registered = RoomRuntimes.Find(DestinationTag);
			return IsReconnectContextCurrent(Context) && Registered
				&& Registered->RegistrationId == DestinationRegistration && Registered->RoomVolume.Get() == Room;
		};
		for (const bool bFallbackOnly : {false, true})
		{
			if (!Anchor && !bFallbackOnly)
			{
				continue;
			}
			FVector Destination;
			if (!FindJoinDestination(Player, Anchor, Room, bFallbackOnly, Destination)
				|| !IsDestinationCurrent()
				|| !IsSafeJoinDestination(Player, Anchor, Room, Destination)
				|| !Player->TeleportTo(Destination, Player->GetActorRotation(), false, true))
			{
				continue;
			}
			Player->GetCapsuleComponent()->UpdateOverlaps();
			if (IsDestinationCurrent() && IsPlayerInsideRoom(Player, Room))
			{
				Context = Current;
				Context.DestinationRoomTag = DestinationTag;
				Context.DestinationRoomRegistrationId = DestinationRegistration;
				UE_LOG(LogTemp, Display,
					TEXT("[RoomCombat] Reconnect joined. Room=%s Player=%s Method=%s Generation=%d"),
					*DestinationTag.ToString(), *GetNameSafe(Player),
					bFallbackOnly ? TEXT("Fallback") : TEXT("Nearby"), Context.GameplayGeneration);
				return true;
			}
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[RoomCombat] Reconnect deferred: no safe room destination. Room=%s Player=%s Generation=%d"),
		*Context.RoomTag.ToString(), *GetNameSafe(Player), Context.GameplayGeneration);
	return false;
}

bool URoomCombatSubsystem::TryStartInitialDetectionForPlayers(FGameplayTag RoomTag)
{
	return TryStartInitialDetectionForPlayers(RoomTag, [] { return true; });
}

bool URoomCombatSubsystem::HasAlivePreplacedEnemy(
	FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime) const
{
	for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Runtime.TrackedAliveEnemies)
	{
		const AEnemyBase* Enemy = EnemyPtr.Get();
		const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
		if (IsValid(Enemy) && !Enemy->IsActorBeingDestroyed() && !Enemy->IsDead()
			&& Registration && Registration->bPreplaced && Registration->RoomTag == RoomTag)
		{
			return true;
		}
	}
	return false;
}

bool URoomCombatSubsystem::BuildEnemyMovementRoomContext(
	const AEnemyBase* Enemy, FEnemyMovementRoomContext& Out) const
{
	Out = FEnemyMovementRoomContext();
	if (!CanRunServerGameplay() || !IsValid(Enemy) || Enemy->IsActorBeingDestroyed()
		|| !Enemy->HasAuthority() || Enemy->GetWorld() != GetWorld()) return false;
	const FGameplayTag Home = Enemy->GetDefaultRoomTag();
	const FRoomCombatRuntime* Source = RoomRuntimes.Find(Home);
	const ARoomVolume* HomeVolume = Source ? Source->RoomVolume.Get() : nullptr;
	if (!IsValid(HomeVolume) || HomeVolume->IsActorBeingDestroyed()
		|| HomeVolume->GetWorld() != GetWorld()) return false;
	Out.HomeRoomTag = Home;
	Out.bAllowGroup = Enemy->GetCombatState() == EEnemyCombatState::Combat
		|| Enemy->GetCombatState() == EEnemyCombatState::Returning;
	Out.Revision = MovementRoomRevision;
	Out.Rooms.Add(Source->RoomVolume);
	if (Out.bAllowGroup && URoomTagComponent::GetRoomGroupTag(Home).IsValid())
	{
		for (const auto& Entry : RoomRuntimes)
		{
			if (Entry.Key == Home || !URoomTagComponent::AreRoomsInSameGroup(Home, Entry.Key)) continue;
			const ARoomVolume* Room = Entry.Value.RoomVolume.Get();
			if (!IsValid(Room) || Room->IsActorBeingDestroyed() || Room->GetWorld() != GetWorld())
			{
				Out = FEnemyMovementRoomContext();
				return false;
			}
			Out.Rooms.Add(Entry.Value.RoomVolume);
		}
	}
	return true;
}

bool URoomCombatSubsystem::IsEnemyMovementRoomContextCurrent(
	const AEnemyBase* Enemy, const FEnemyMovementRoomContext& Context) const
{
	if (!CanRunServerGameplay() || Context.Revision != MovementRoomRevision
		|| !IsValid(Enemy) || Enemy->IsActorBeingDestroyed() || !Enemy->HasAuthority()
		|| Enemy->GetWorld() != GetWorld() || Context.HomeRoomTag != Enemy->GetDefaultRoomTag()
		|| Context.Rooms.IsEmpty()) return false;
	const bool bAllowGroup = Enemy->GetCombatState() == EEnemyCombatState::Combat
		|| Enemy->GetCombatState() == EEnemyCombatState::Returning;
	if (Context.bAllowGroup != bAllowGroup) return false;
	const FRoomCombatRuntime* Source = RoomRuntimes.Find(Context.HomeRoomTag);
	if (!Source || Context.Rooms[0] != Source->RoomVolume) return false;
	for (const TWeakObjectPtr<ARoomVolume>& Room : Context.Rooms)
	{
		if (!Room.IsValid() || Room->IsActorBeingDestroyed() || Room->GetWorld() != GetWorld()) return false;
	}
	return true;
}

bool URoomCombatSubsystem::TryStartCombatFromPursuit(AEnemyBase* Enemy, FGameplayTag EnteredRoomTag)
{
	if (!CanRunServerGameplay() || !IsValid(Enemy) || !Enemy->HasAuthority()
		|| Enemy->GetWorld() != GetWorld())
	{
		return false;
	}
	const FGameplayTag SourceTag = Enemy->GetDefaultRoomTag();
	const FRoomCombatRuntime* Source = RoomRuntimes.Find(SourceTag);
	const FRoomCombatRuntime* Destination = RoomRuntimes.Find(EnteredRoomTag);
	if (SourceTag == EnteredRoomTag || !URoomTagComponent::AreRoomsInSameGroup(SourceTag, EnteredRoomTag)
		|| !Source || !Destination || Destination->State != ERoomCombatState::Dormant)
	{
		return false;
	}
	const FGuid SourceId = Source->RegistrationId;
	const FGuid DestinationId = Destination->RegistrationId;
	const int32 Lease = Enemy->GetPoolLeaseSerial();
	const int32 Generation = Enemy->GetPoolGameplayGeneration();
	// 시작 이벤트가 Reset/언로드/풀 반환을 일으킬 수 있다. 최초 판정뿐 아니라
	// 합류와 전투 확정 사이에도 같은 적·방 수명과 실제 진입 상태를 재검사한다.
	auto IsPursuitCurrent = [this, WeakEnemy = TWeakObjectPtr<AEnemyBase>(Enemy), SourceTag, EnteredRoomTag,
		SourceId, DestinationId, Lease, Generation]()
	{
		AEnemyBase* Enemy = WeakEnemy.Get();
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed() || Enemy->IsDead()
			|| Enemy->GetCombatState() != EEnemyCombatState::Combat
			|| Enemy->IsAIControlSuppressed() || !Enemy->CanUseRoomTargetSharing()
			|| Enemy->GetDefaultRoomTag() != SourceTag || Enemy->GetCurrentRoomTag() != EnteredRoomTag
			|| (Enemy->IsPoolManaged() && (Enemy->GetEnemyPoolState() != EEnemyPoolState::CombatActive
				|| !Enemy->MatchesPoolLease(Generation, Lease))))
		{
			return false;
		}
		const FRoomCombatRuntime* CurrentSource = RoomRuntimes.Find(SourceTag);
		const FRoomCombatRuntime* CurrentDestination = RoomRuntimes.Find(EnteredRoomTag);
		const TWeakObjectPtr<AEnemyBase> EnemyPtr(Enemy);
		const FRoomCombatEnemyRegistration* Registration = RegisteredEnemies.Find(EnemyPtr);
		ARoomVolume* Room = CurrentDestination ? CurrentDestination->RoomVolume.Get() : nullptr;
		return CurrentSource && CurrentSource->RegistrationId == SourceId
			&& CurrentSource->RoomVolume.IsValid() && IsActiveCombatRuntime(SourceTag, *CurrentSource)
			&& CurrentSource->TrackedAliveEnemies.Contains(EnemyPtr)
			&& Registration && Registration->RoomTag == SourceTag
			&& CurrentDestination && CurrentDestination->RegistrationId == DestinationId
			&& IsValid(Room) && !Room->IsActorBeingDestroyed()
			&& IsInitialDetectionPhase(EnteredRoomTag, *CurrentDestination)
			&& HasAlivePreplacedEnemy(EnteredRoomTag, *CurrentDestination)
			// Capsule 가장자리가 먼저 입장할 수 있다. 중심점만 요구하면 이후에는
			// Room 변경 이벤트가 없어 확산을 놓치므로 실제 overlap도 입장으로 인정한다.
			&& (Room->ContainsWorldLocation(Enemy->GetActorLocation()) || Room->IsOverlappingActor(Enemy));
	};
	if (!IsPursuitCurrent()
		|| !TryStartInitialDetectionForPlayers(EnteredRoomTag, IsPursuitCurrent))
	{
		return false;
	}
	// 방의 시작 조건/플레이어 합류가 확정된 뒤 도착 방 소속 AI에만 전파한다.
	// 들어온 적은 원래 방 등록에 남으므로 도착 방 생존 수에 추가되지 않는다.
	UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
	return IsPursuitCurrent() && Rooms
		&& Rooms->NotifyRoomCombat(EnteredRoomTag, Enemy->GetLastKnownPlayerLocation(), nullptr);
}

bool URoomCombatSubsystem::TryStartInitialDetectionForPlayers(
	FGameplayTag RoomTag, TFunction<bool()> IsRequestCurrent)
{
	// 실제 감지 진입점: 플레이어 위치 확인 -> Room 예약 -> 필요 시 합류 -> 차단막/Wave 확정.
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	ARoomVolume* Room = Runtime ? Runtime->RoomVolume.Get() : nullptr;
	if (!CanRunServerGameplay() || !IsRequestCurrent() || IsRoomStartHeld(RoomTag) || !Room || !GetWorld())
	{
		return false;
	}
	if (Runtime->State == ERoomCombatState::Combat)
	{
		return IsRoomCombatReserved(RoomTag);
	}
	if (Runtime->State == ERoomCombatState::Preparing)
	{
		return false;
	}
	if (Runtime->State != ERoomCombatState::Dormant || !CanReserveCombatRoom(RoomTag)
		|| GetWorld()->GetTimeSeconds() < Runtime->NextPreparationAllowedTimeSeconds)
	{
		return false;
	}
	AGameStateBase* GameState = GetWorld()->GetGameState();
	AShooterCharacter* Shooter = nullptr;
	APartnerCharacter* Partner = nullptr;
	AOutlierPlayerState* PairState = nullptr;
	if (GameState)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (AOutlierPlayerState* OutlierState = Cast<AOutlierPlayerState>(PlayerState))
			{
				Shooter = OutlierState->GetShooterCharacter();
				Partner = OutlierState->GetPartnerCharacter();
				if (IsValid(Shooter) && IsValid(Partner))
				{
					PairState = OutlierState;
					break;
				}
			}
		}
	}
	if (!IsValid(Shooter) || !IsValid(Partner))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomCombat] Detection deferred: player pair unavailable. Room=%s"),
			*RoomTag.ToString());
		return false;
	}
	ARoomVolume* ShooterRoom = FindPlayerRoomInCombatGroup(Shooter, RoomTag);
	ARoomVolume* PartnerRoom = FindPlayerRoomInCombatGroup(Partner, RoomTag);
	const bool bShooterInside = ShooterRoom != nullptr;
	const bool bPartnerInside = PartnerRoom != nullptr;
	// 발각시킨 Actor가 누구인지는 사용하지 않는다. 두 명 모두 밖이면 Enemy도 Battle로 넘기지 않는다.
	if (!bShooterInside && !bPartnerInside)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] Detection deferred: no player inside. Room=%s ShooterRoom=%s ShooterLocation=%s PartnerRoom=%s PartnerLocation=%s"),
			*RoomTag.ToString(), *Shooter->GetCurrentRoomTag().ToString(), *Shooter->GetActorLocation().ToString(),
			*Partner->GetCurrentRoomTag().ToString(), *Partner->GetActorLocation().ToString());
		return false;
	}
	// 전투 귀속과 합류 목적지는 구분한다. 요청 방에 플레이어가 있으면 그쪽을,
	// 형제 방에서 감지된 경우에는 실제 내부 플레이어의 방을 기준으로 합류한다.
	const bool bAnchorIsShooter = bShooterInside && (ShooterRoom == Room || PartnerRoom != Room);
	AFirstPersonCharacter* Anchor = bAnchorIsShooter ? static_cast<AFirstPersonCharacter*>(Shooter)
		: static_cast<AFirstPersonCharacter*>(Partner);
	AFirstPersonCharacter* OtherPlayer = bAnchorIsShooter ? static_cast<AFirstPersonCharacter*>(Partner)
		: static_cast<AFirstPersonCharacter*>(Shooter);
	// 전투 귀속은 RoomTag에 남긴다. 합류 목적지는 내부 플레이어가 실제로 있는 방이며,
	// 목적지 방의 전투를 새로 시작하거나 기존 문/해킹 조건을 충족시키지 않는다.
	ARoomVolume* JoinRoom = bAnchorIsShooter ? ShooterRoom : PartnerRoom;
	const FGameplayTag JoinRoomTag = JoinRoom->GetRoomTag();
	const FGuid JoinRegistrationId = RoomRuntimes.FindChecked(JoinRoomTag).RegistrationId;
	// 차단막의 현재 충돌 상태가 아니라 대상 방에 직접 연결된 등록 Actor의 존재를 본다.
	// 그룹 전체를 막을 수 있다는 이유만으로 다른 형제 방의 차단막을 대신 사용하지 않는다.
	auto HasJoinBarrier = [this, JoinRoomTag]()
	{
		return RegisteredBarriers.ContainsByPredicate([this, JoinRoomTag](const TWeakObjectPtr<ARoomCombatBarrier>& Entry)
		{
			const ARoomCombatBarrier* Barrier = Entry.Get();
			return IsValid(Barrier) && !Barrier->IsActorBeingDestroyed()
				&& Barrier->GetWorld() == GetWorld() && Barrier->ServesRoom(JoinRoomTag);
		});
	};
	const bool bSuitAcquired = PairState->IsPairSuitAcquired();
	const bool bHasJoinBarrier = HasJoinBarrier();
	const bool bRequiresSameRoom = bSuitAcquired && bHasJoinBarrier;
	AFirstPersonCharacter* MovingPlayer = bRequiresSameRoom && ShooterRoom != PartnerRoom ? OtherPlayer : nullptr;
	// 이동이 없어도 두 내부 방의 수명은 보존한다. 준비/시작 콜백 중 재등록한 방을
	// 이전 합류의 근거로 재사용하지 않으며, 위치는 완료 직전에 다시 확인한다.
	ARoomVolume* OtherRoom = bAnchorIsShooter ? PartnerRoom : ShooterRoom;
	const FGameplayTag OtherInsideTag = OtherRoom ? OtherRoom->GetRoomTag() : FGameplayTag();
	const FGuid OtherRegistrationId = OtherInsideTag.IsValid()
		? RoomRuntimes.FindChecked(OtherInsideTag).RegistrationId : FGuid();
	const auto WeakShooter = TWeakObjectPtr<AShooterCharacter>(Shooter);
	const auto WeakPartner = TWeakObjectPtr<APartnerCharacter>(Partner);
	const auto WeakPair = TWeakObjectPtr<AOutlierPlayerState>(PairState);
	const auto WeakRoom = TWeakObjectPtr<ARoomVolume>(JoinRoom);
	const auto WeakAnchor = TWeakObjectPtr<AFirstPersonCharacter>(Anchor);
	const auto ShooterController = TWeakObjectPtr<AController>(Shooter->GetController());
	const auto PartnerController = TWeakObjectPtr<AController>(Partner->GetController());
	const auto ShooterState = TWeakObjectPtr<APlayerState>(Shooter->GetPlayerState());
	const auto PartnerState = TWeakObjectPtr<APlayerState>(Partner->GetPlayerState());
	const int32 PairId = PairState->GetPairId();
	// Query가 반환될 때까지 살아 있는 값/약한 참조만 캡처한다. 스택 람다의 참조를 연장하지 않는다.
	auto IsJoinRoomCurrent = [this, IsRequestCurrent, WeakShooter, WeakPartner, WeakPair, WeakRoom, WeakAnchor,
		ShooterController, PartnerController, ShooterState, PartnerState, PairId,
		JoinRoomTag, JoinRegistrationId, OtherInsideTag, OtherRegistrationId, bSuitAcquired, bHasJoinBarrier, HasJoinBarrier]()
	{
		AShooterCharacter* Shooter = WeakShooter.Get();
		APartnerCharacter* Partner = WeakPartner.Get();
		AOutlierPlayerState* Pair = WeakPair.Get();
		const FRoomCombatRuntime* JoinRuntime = RoomRuntimes.Find(JoinRoomTag);
		const FRoomCombatRuntime* OtherRuntime = RoomRuntimes.Find(OtherInsideTag);
		if (!IsRequestCurrent() || !IsValid(Pair) || !IsValid(Shooter) || !IsValid(Partner)
			|| Shooter->IsActorBeingDestroyed() || Partner->IsActorBeingDestroyed() || Shooter->IsDead()
			|| ShooterController.IsStale() || PartnerController.IsStale() || ShooterState.IsStale() || PartnerState.IsStale()
			|| TWeakObjectPtr<AController>(Shooter->GetController()) != ShooterController
			|| TWeakObjectPtr<AController>(Partner->GetController()) != PartnerController
			|| TWeakObjectPtr<APlayerState>(Shooter->GetPlayerState()) != ShooterState
			|| TWeakObjectPtr<APlayerState>(Partner->GetPlayerState()) != PartnerState
			|| Pair->GetPairId() != PairId || Pair->GetShooterCharacter() != Shooter || Pair->GetPartnerCharacter() != Partner
			|| Pair->IsPairSuitAcquired() != bSuitAcquired || HasJoinBarrier() != bHasJoinBarrier
			|| !JoinRuntime || JoinRuntime->RegistrationId != JoinRegistrationId || JoinRuntime->RoomVolume != WeakRoom
			|| !IsPlayerInsideRoom(WeakAnchor.Get(), WeakRoom.Get())
			|| (OtherInsideTag.IsValid() && (!OtherRuntime || !OtherRuntime->RoomVolume.IsValid()
				|| OtherRuntime->RegistrationId != OtherRegistrationId))) return false;
		if (Shooter->IsSuitTransitionBlocked() || Partner->IsSuitTransitionBlocked()) return false;
		for (const auto& State : {ShooterState, PartnerState})
		{
			const AOutlierPlayerState* Participant = Cast<AOutlierPlayerState>(State.Get());
			if (Participant && (Participant->GetPairId() != PairId || Participant->GetShooterCharacter() != Shooter
				|| Participant->GetPartnerCharacter() != Partner)) return false;
		}
		for (const UOutlierAbilitySystemComponent* ASC : {Shooter->GetOutlierAbilitySystemComponent(), Partner->GetOutlierAbilitySystemComponent()})
		{
			if (ASC && (ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
				|| ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))) return false;
		}
		return true;
	};
	auto ArePlayersJoined = [this, WeakShooter, WeakPartner, WeakRoom, bRequiresSameRoom, IsJoinRoomCurrent]()
	{
		// 슈트 전/차단막 없는 방은 이동 생략이 정상이다. 이때 상대의 합류를 요구하면
		// 전투가 영구 대기하므로 기준 플레이어와 준비 수명만 확인해 진행한다.
		return IsJoinRoomCurrent() && (!bRequiresSameRoom
			|| (IsPlayerInsideRoom(WeakShooter.Get(), WeakRoom.Get()) && IsPlayerInsideRoom(WeakPartner.Get(), WeakRoom.Get())));
	};
	FRoomCombatPreparationContext Context;
	if (!BeginInitialDetectionPreparation(RoomTag, Anchor, Context))
	{
		return false;
	}
	// 1. 이동이 필요한 시도만 비동기로 보관한다. 반환 false는 아직 합류가 완료되지 않았다는 뜻이다.
	if (MovingPlayer)
	{
		const float Timeout = CVarInitialDetectionJoinTimeoutSeconds.GetValueOnGameThread();
		if (!FMath::IsFinite(Timeout) || Timeout <= 0 || !IsJoinRoomCurrent())
		{
			CancelInitialDetectionPreparation(Context);
			return false;
		}
		FInitialDetectionJoin Join;
		Join.Preparation = Context;
		Join.MovingPlayer = MovingPlayer;
		Join.Anchor = Anchor;
		Join.PairState = PairState;
		Join.DestinationRoom = JoinRoom;
		Join.DestinationRoomTag = JoinRoomTag;
		Join.OtherRoomTag = OtherInsideTag;
		Join.Reference = FTransform(FRotator(0, Anchor->GetActorRotation().Yaw, 0), Anchor->GetActorLocation());
		Join.IsCurrent = IsJoinRoomCurrent;
		Join.Deadline = GetWorld()->GetTimeSeconds() + Timeout;
		Join.RealDeadline = FPlatformTime::Seconds() + Timeout;
		const FGuid Id = Context.PreparationId;
		const TWeakObjectPtr<URoomCombatSubsystem> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimer(Join.Timeout, FTimerDelegate::CreateLambda([WeakThis, Id, Context]
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->ClearInitialDetectionJoin(Id);
			WeakThis->CancelInitialDetectionPreparation(Context);
		}), Timeout, false);
		InitialDetectionJoins.Add(Id, MoveTemp(Join));
		StartInitialDetectionJoinQuery(Id, false);
		return false;
	}
	if (!CompleteInitialDetectionPreparation(Context, ArePlayersJoined))
	{
		if (BarrierClosurePreparations.Contains(Context.RoomTag)) return false;
		ClearForcedJoinProtection(Context.PreparationId);
		CancelInitialDetectionPreparation(Context);
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Players joined. Room=%s Anchor=%s Moved=%s Generation=%d"),
		*RoomTag.ToString(), *GetNameSafe(Anchor), *GetNameSafe(MovingPlayer), Context.GameplayGeneration);
	return true;
}

bool URoomCombatSubsystem::IsInitialDetectionJoinCurrent(FGuid Id) const
{
	const FInitialDetectionJoin* Join = InitialDetectionJoins.Find(Id);
	return Join && IsPreparationContextCurrent(Join->Preparation) && Join->IsCurrent()
		&& GetWorld()->GetTimeSeconds() < Join->Deadline && FPlatformTime::Seconds() < Join->RealDeadline;
}

void URoomCombatSubsystem::ClearInitialDetectionJoin(FGuid Id)
{
	FInitialDetectionJoin Join;
	if (!InitialDetectionJoins.RemoveAndCopyValue(Id, Join)) return;
	GetWorld()->GetTimerManager().ClearTimer(Join.Timeout);
	TObjectPtr<UPlayerRelocationQuery> Query;
	InitialDetectionQueries.RemoveAndCopyValue(Id, Query);
	// 소유권을 먼저 지운다. Abort가 동기 완료를 호출해도 새 시도를 정리할 수 없다.
	if (Query) Query->Cancel();
}

void URoomCombatSubsystem::CancelInitialDetectionJoinsForRoom(FGameplayTag RoomTag)
{
	TArray<FGuid> Ids;
	for (const auto& Entry : InitialDetectionJoins)
		if (Entry.Value.Preparation.RoomTag == RoomTag || Entry.Value.DestinationRoomTag == RoomTag
			|| Entry.Value.OtherRoomTag == RoomTag) Ids.Add(Entry.Key);
	for (FGuid Id : Ids)
	{
		const FInitialDetectionJoin* Join = InitialDetectionJoins.Find(Id);
		if (!Join) continue;
		const FRoomCombatPreparationContext Context = Join->Preparation;
		ClearInitialDetectionJoin(Id);
		CancelInitialDetectionPreparation(Context);
	}
}

bool URoomCombatSubsystem::IsCombatJoinCapsuleSafe(const ARoomVolume* Room,
	const FVector& Location, float Radius, float HalfHeight) const
{
	// 실제 목적 자식 방 전체 포함을 검사한다. 현재 꺼져 있어도 곧 닫힐 외곽 차단막은 제외한다.
	FVector Clamped;
	if (!IsValid(Room) || !Room->TryClampFlightLocation(Location, FVector(Radius, Radius, HalfHeight), Clamped)
		|| !Clamped.Equals(Location, 0.01f)) return false;
	for (const TWeakObjectPtr<ARoomCombatBarrier>& Entry : RegisteredBarriers)
		if (const ARoomCombatBarrier* Barrier = Entry.Get(); IsValid(Barrier)
			&& Barrier->CanBlockRoom(Room->GetRoomTag()) && Barrier->OverlapsJoinCapsule(Location, Radius, HalfHeight)) return false;
	return true;
}

void URoomCombatSubsystem::StartInitialDetectionJoinQuery(FGuid Id, bool bFallback)
{
	// 2. 같은 저장 기준/전체 기한으로 주변 EQS -> 지정 출입구 순서만 진행한다.
	const FInitialDetectionJoin* Stored = InitialDetectionJoins.Find(Id);
	if (!Stored) return;
	const FInitialDetectionJoin Join = *Stored;
	if (!IsInitialDetectionJoinCurrent(Id))
	{
		ClearInitialDetectionJoin(Id);
		CancelInitialDetectionPreparation(Join.Preparation);
		return;
	}
	AFirstPersonCharacter* Moving = Join.MovingPlayer.Get();
	FPlayerRelocationRequest Request;
	Request.MovingCharacter = Moving;
	Request.Anchor = Join.Anchor.Get();
	Request.Reference = Join.Reference;
	Request.Mode = Cast<AShooterCharacter>(Moving) ? EPlayerRelocationMode::Ground : EPlayerRelocationMode::Flight;
	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(Moving))
		Request.bWantsCrouch = Shooter->FindComponentByClass<UShooterMovementComponent>()->WantsToCrouch();
	Request.TimeoutSeconds = FMath::Min(Join.Deadline - GetWorld()->GetTimeSeconds(), Join.RealDeadline - FPlatformTime::Seconds());
	const TWeakObjectPtr<URoomCombatSubsystem> WeakThis(this);
	Request.IsContextCurrent = [WeakThis, Id] { return WeakThis.IsValid() && WeakThis->IsInitialDetectionJoinCurrent(Id); };
	Request.AdditionalFilter = [WeakThis, Room = Join.DestinationRoom](const FVector& Location, float Radius, float Height)
	{
		return WeakThis.IsValid() && WeakThis->IsCombatJoinCapsuleSafe(Room.Get(), Location, Radius, Height);
	};
	if (bFallback)
	{
		// 지정 출입구만 가까운 순서대로 검사한다. 주변 재생성/무제한 범위 확장은 하지 않는다.
		TArray<TWeakObjectPtr<ARoomCombatBarrier>> Fallbacks;
		for (const auto& Entry : RegisteredBarriers)
			if (Entry.IsValid() && Entry->ServesRoom(Join.DestinationRoomTag)) Fallbacks.Add(Entry);
		const FVector Reference = Join.Reference.GetLocation();
		Fallbacks.StableSort([Reference](const auto& Left, const auto& Right)
		{
			return FVector::DistSquared(Reference, Left->GetActorLocation()) < FVector::DistSquared(Reference, Right->GetActorLocation());
		});
		for (const auto& Entry : Fallbacks)
		{
			if (Request.ExplicitCandidates.Num() >= Request.MaxCandidatesPerStage) break;
			Request.ExplicitCandidates.Add(Entry->GetJoinFallbackLocation());
		}
		if (Request.ExplicitCandidates.IsEmpty())
		{
			ClearInitialDetectionJoin(Id);
			CancelInitialDetectionPreparation(Join.Preparation);
			return;
		}
	}
	UPlayerRelocationQuery* Query = NewObject<UPlayerRelocationQuery>(this);
	InitialDetectionQueries.Add(Id, Query);
	Query->Start(Request, [WeakThis, Id, bFallback](const FPlayerRelocationResult& Result)
	{
		if (WeakThis.IsValid()) WeakThis->CompleteInitialDetectionJoinQuery(Id, bFallback, Result);
	});
}

void URoomCombatSubsystem::CompleteInitialDetectionJoinQuery(FGuid Id, bool bFallback, const FPlayerRelocationResult& Result)
{
	// 3. 현재 결과만 최종 검사 -> 자세/이동 -> 실제 Room -> 보호 -> 전투 확정으로 진행한다.
	const FInitialDetectionJoin* Stored = InitialDetectionJoins.Find(Id);
	if (!Stored) return;
	const FInitialDetectionJoin Join = *Stored;
	auto Fail = [this, Id, Join, bFallback, Status = Result.Status](bool bAllowFallback)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] Join attempt rejected. Room=%s DestinationRoom=%s Method=%s QueryStatus=%d Moving=%s CurrentRoom=%s"),
			*Join.Preparation.RoomTag.ToString(), *Join.DestinationRoomTag.ToString(),
			bFallback ? TEXT("Fallback") : TEXT("EQS"), static_cast<int32>(Status), *GetNameSafe(Join.MovingPlayer.Get()),
			Join.MovingPlayer.IsValid() ? *Join.MovingPlayer->GetCurrentRoomTag().ToString() : TEXT("None"));
		if (bAllowFallback && !bFallback && IsInitialDetectionJoinCurrent(Id))
		{
			StartInitialDetectionJoinQuery(Id, true);
			return;
		}
		ClearInitialDetectionJoin(Id);
		CancelInitialDetectionPreparation(Join.Preparation);
	};
	if (!IsInitialDetectionJoinCurrent(Id)) { Fail(false); return; }
	if (Result.Status != EPlayerRelocationStatus::Success)
	{
		Fail(Result.Status == EPlayerRelocationStatus::NoCandidate);
		return;
	}
	UPlayerRelocationQuery* Query = InitialDetectionQueries.FindRef(Id);
	AFirstPersonCharacter* Moving = Join.MovingPlayer.Get();
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(Moving);
	auto Revalidate = [Query, Shooter]
	{
		return Query && (Shooter
			? Query->RevalidateForCrouchIntent(Shooter->FindComponentByClass<UShooterMovementComponent>()->WantsToCrouch())
			: Query->Revalidate());
	};
	if (!Revalidate()) { Fail(true); return; }
#if WITH_DEV_AUTOMATION_TESTS
	if (JoinTeleportObserverForTesting) JoinTeleportObserverForTesting();
#endif
	// 테스트 hook/자세 입력/Overlap은 재진입할 수 있다. 이동 직전까지 같은 요청과 공간이어야 한다.
	if (!IsInitialDetectionJoinCurrent(Id)) { Fail(false); return; }
	if (!Revalidate()) { Fail(true); return; }
	const FPlayerRelocationResult Landing = Query->GetResult();
	UShooterCharacterMovementComponent* ShooterMovement = Shooter
		? Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement()) : nullptr;
	const bool bMoved = Shooter ? ShooterMovement && ShooterMovement->TeleportToRelocation(Landing.Location,
		Landing.Posture == EPlayerRelocationPosture::Crouched)
		: Moving->TeleportTo(Landing.Location, Moving->GetActorRotation(), false, true);
	if (!bMoved) { Fail(true); return; }
	if (!IsInitialDetectionJoinCurrent(Id)) { Fail(false); return; }
	Moving->GetCapsuleComponent()->UpdateOverlaps();
	if (!IsInitialDetectionJoinCurrent(Id) || !Revalidate()
		|| !Moving->GetActorLocation().Equals(Landing.Location, 0.1f)
		|| !IsPlayerInsideRoom(Moving, Join.DestinationRoom.Get())) { Fail(false); return; }
	Moving->SetBase(nullptr);
	Moving->GetCharacterMovement()->StopMovementImmediately();
	Moving->ForceNetUpdate();
	// EQS 수락은 완료가 아니다. 실제 이동/Room 소속을 확인한 대상만 보호하고 전투를 확정한다.
	if (!ApplyForcedJoinProtection(Join.Preparation, Moving, Join.PairState.Get(), Join.DestinationRoomTag)
		|| !IsInitialDetectionJoinCurrent(Id)
		|| !CompleteInitialDetectionPreparation(Join.Preparation, [this, Id, Join]
		{
			return IsInitialDetectionJoinCurrent(Id) && IsPlayerInsideRoom(Join.MovingPlayer.Get(), Join.DestinationRoom.Get());
		}))
	{
		if (BarrierClosurePreparations.Contains(Join.Preparation.RoomTag)) return;
		Fail(false);
		return;
	}
	ClearInitialDetectionJoin(Id);
	// 감지 호출은 pending 동안 false를 돌려준다. 완료 뒤 기존 전파 경로를 다시 열어 추가 감지를 기다리지 않는다.
	// Wave 시작도 외부 콜백을 실행한다. Preparing 토큰은 이미 끝났으므로 완료된 전투의 등록/세대를 새로 확인한다.
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Join.Preparation.RoomTag);
	if (CanRunServerGameplay() && Join.IsCurrent() && Join.Anchor.IsValid() && Runtime
		&& Runtime->RegistrationId == Join.Preparation.RoomRegistrationId
		&& Runtime->GameplayGeneration == Join.Preparation.GameplayGeneration
		&& IsActiveCombatRuntime(Join.Preparation.RoomTag, *Runtime))
		if (UEnemyRoomSubsystem* Rooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			Rooms->NotifyRoomCombat(Join.Preparation.RoomTag, Join.Anchor->GetActorLocation(), nullptr);
}


bool URoomCombatSubsystem::ApplyForcedJoinProtection(
	const FRoomCombatPreparationContext& Context, AFirstPersonCharacter* Player,
	AOutlierPlayerState* PlayerState, FGameplayTag DestinationRoomTag)
{
	if (!IsPreparationContextCurrent(Context) || !IsValid(Player) || !IsValid(PlayerState))
	{
		return false;
	}
	const float Duration = CombatDefinition ? CombatDefinition->ForcedJoinInvulnerabilitySeconds : 0.0f;
	if (!FMath::IsFinite(Duration) || Duration < 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Invalid ForcedJoinInvulnerabilitySeconds: %f"), Duration);
		return false;
	}
	if (Duration == 0.0f || ForcedJoinProtections.Contains(Context.PreparationId))
	{
		return true;
	}
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(Player);
	APartnerCharacter* Partner = Cast<APartnerCharacter>(Player);
	UOutlierAbilitySystemComponent* AbilitySystem = Shooter ? Shooter->GetOutlierAbilitySystemComponent()
		: (Partner ? Partner->GetOutlierAbilitySystemComponent() : nullptr);
	if (!AbilitySystem || AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}

	const FActiveGameplayEffectHandle Effect = AbilitySystem->ApplyTimedGameplayEffectToSelf(
		UOutlierDamageImmuneGameplayEffect::StaticClass(), Duration, Player);
	// 효과 적용의 태그 콜백에서 사망/Reset/교체될 수 있다. 실패한 시도의 효과만 되돌린다.
	if (!Effect.IsValid() || !IsPreparationContextCurrent(Context) || !IsValid(Player)
		|| !IsValid(PlayerState) || AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
		|| (PlayerState->GetShooterCharacter() != Player && PlayerState->GetPartnerCharacter() != Player))
	{
		AbilitySystem->RemoveActiveEffectFromSelf(Effect);
		return false;
	}

	const FGuid Id = Context.PreparationId;
	FForcedJoinProtection& Protection = ForcedJoinProtections.Add(Id);
	Protection.RoomTag = Context.RoomTag;
	Protection.DestinationRoomTag = DestinationRoomTag;
	Protection.Player = Player;
	Protection.PlayerState = PlayerState;
	Protection.AbilitySystem = AbilitySystem;
	Protection.EffectHandle = Effect;
	Protection.DeathHandle = AbilitySystem->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Dead(), EGameplayTagEventType::NewOrRemoved)
		.AddWeakLambda(this, [this, Id](FGameplayTag, int32 Count)
		{
			if (Count > 0) ClearForcedJoinProtection(Id);
		});
	// 기존 Pawn 무효화 알림은 UnPossessed와 EndPlay 양쪽에서 발생한다.
	// 별도 Tick 없이 제어권 교체/종료 순간에 보호와 구독을 함께 정리한다.
	Protection.PawnInvalidatedHandle = Player->OnSuitTransitionParticipantInvalidated.AddWeakLambda(
		this, [this, Id](AFirstPersonCharacter*) { ClearForcedJoinProtection(Id); });
	Protection.CharactersChangedHandle = PlayerState->OnPlayerCharactersChanged.AddWeakLambda(
		this, [this, Id](AOutlierPlayerState* State)
		{
			const FForcedJoinProtection* Current = ForcedJoinProtections.Find(Id);
			if (Current && State->GetShooterCharacter() != Current->Player.Get()
				&& State->GetPartnerCharacter() != Current->Player.Get())
			{
				ClearForcedJoinProtection(Id);
			}
		});
	if (auto* Removed = AbilitySystem->OnGameplayEffectRemoved_InfoDelegate(Effect))
	{
		// 자연 만료도 같은 정리 경로로 보낸다. 별도 만료 Timer는 만들지 않는다.
		Protection.EffectRemovedHandle = Removed->AddWeakLambda(this,
			[this, Id](const FGameplayEffectRemovalInfo&) { ClearForcedJoinProtection(Id, false); });
	}
	return true;
}

void URoomCombatSubsystem::ClearForcedJoinProtection(FGuid PreparationId, bool bRemoveEffect)
{
	FForcedJoinProtection Protection;
	// 제거 이벤트가 재진입해도 같은 효과를 두 번 정리하지 않도록 소유 기록부터 분리한다.
	if (!ForcedJoinProtections.RemoveAndCopyValue(PreparationId, Protection))
	{
		return;
	}
	if (AFirstPersonCharacter* Player = Protection.Player.Get())
	{
		Player->OnSuitTransitionParticipantInvalidated.Remove(Protection.PawnInvalidatedHandle);
	}
	if (AOutlierPlayerState* State = Protection.PlayerState.Get())
	{
		State->OnPlayerCharactersChanged.Remove(Protection.CharactersChangedHandle);
	}
	if (UOutlierAbilitySystemComponent* AbilitySystem = Protection.AbilitySystem.Get())
	{
		AbilitySystem->RegisterGameplayTagEvent(OutlierGameplayTags::State::Dead(),
			EGameplayTagEventType::NewOrRemoved).Remove(Protection.DeathHandle);
		if (auto* Removed = AbilitySystem->OnGameplayEffectRemoved_InfoDelegate(Protection.EffectHandle))
		{
			Removed->Remove(Protection.EffectRemovedHandle);
		}
		if (bRemoveEffect) AbilitySystem->RemoveActiveEffectFromSelf(Protection.EffectHandle);
	}
}

void URoomCombatSubsystem::ClearForcedJoinProtectionsForRoom(FGameplayTag RoomTag)
{
	TArray<FGuid> ToRemove;
	for (const auto& Entry : ForcedJoinProtections)
	{
		if (Entry.Value.RoomTag == RoomTag || Entry.Value.DestinationRoomTag == RoomTag)
		{
			ToRemove.Add(Entry.Key);
		}
	}
	for (FGuid Id : ToRemove) ClearForcedJoinProtection(Id);
}

bool URoomCombatSubsystem::BeginInitialDetectionPreparation(
	FGameplayTag RoomTag,
	AActor* AnchorPlayer,
	FRoomCombatPreparationContext& OutContext)
{
	OutContext = FRoomCombatPreparationContext();
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag) || !IsValid(AnchorPlayer)
		|| AnchorPlayer->IsActorBeingDestroyed()
		|| !AnchorPlayer->HasAuthority() || AnchorPlayer->GetWorld() != GetWorld()
		|| !RoomTag.IsValid())
	{
		return false;
	}

	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || !Runtime->RoomVolume.IsValid() || Runtime->bSavedCurrentPhasePending
		|| Runtime->bPhaseCleanupInProgress)
	{
		return false;
	}
	const bool bNewPreparation = Runtime->State != ERoomCombatState::Preparing;
	if (!bNewPreparation)
	{
		// 다른 적이 다시 발각해도 방 안의 기준 플레이어와 준비 수명은 그대로 유지한다.
		if (!IsRoomCombatReserved(RoomTag) || !Runtime->PreparingAnchorPlayer.IsValid())
		{
			return false;
		}
	}
	else
	{
		if (Runtime->State != ERoomCombatState::Dormant
			|| !CanReserveCombatRoom(RoomTag)
			|| GetWorld()->GetTimeSeconds() < Runtime->NextPreparationAllowedTimeSeconds)
		{
			return false;
		}
		CompactAliveEnemies(*Runtime);
		if ((Runtime->TrackedAliveEnemies.IsEmpty() && !Runtime->bInitialDetectionPendingJoin)
			|| !IsInitialDetectionPhase(RoomTag, *Runtime))
		{
			return false;
		}

		// 합류가 끝날 때까지 이 Room을 점유하고 배치 적의 진행 중 공격도 끊는다.
		Runtime->PreparingAnchorPlayer = AnchorPlayer;
		Runtime->PreparationId = FGuid::NewGuid();
		// 한 번 발각된 차수는 합류 실패로 발각 전 암살 상태가 되지 않는다.
		// 준비 중 전멸했더라도 다음 유효 시도의 합류 완료까지 증원을 보류한다.
		Runtime->bInitialDetectionPendingJoin = true;
		Runtime->State = ERoomCombatState::Preparing;
		Runtime->bPreparationOwnsReservation = !IsRoomCombatReserved(RoomTag);
		Runtime->bPreparationExitBlockWasActive = Runtime->bExitBlockActive;
		ReserveCombatRoom(RoomTag);
		UE_LOG(LogTemp, Display,
			TEXT("[RoomCombat] Initial detection preparing. Room=%s Player=%s Generation=%d"),
			*RoomTag.ToString(), *GetNameSafe(AnchorPlayer), Runtime->GameplayGeneration);
	}

	// 합류 처리 쪽은 이 토큰을 보관했다가 이동과 Room 소속 확인 후 완료를 요청한다.
	OutContext.RoomTag = RoomTag;
	OutContext.AnchorPlayer = Runtime->PreparingAnchorPlayer;
	OutContext.RoomRegistrationId = Runtime->RegistrationId;
	OutContext.PreparationId = Runtime->PreparationId;
	OutContext.GameplayGeneration = Runtime->GameplayGeneration;
	if (bNewPreparation)
	{
		// 공격 중단도 외부 동작을 종료한다. 콜백 전에 토큰과 목록을 복사하고,
		// Reset/취소가 발생하면 이전 Runtime 포인터와 반복자를 다시 사용하지 않는다.
		const TArray<TWeakObjectPtr<AEnemyBase>> Enemies = Runtime->TrackedAliveEnemies.Array();
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : Enemies)
		{
			if (!IsPreparationContextCurrent(OutContext))
			{
				return false;
			}
			if (AEnemyBase* Enemy = EnemyPtr.Get(); Enemy && !Enemy->IsEnemyPossessed())
			{
				Enemy->StopCurrentAttack();
			}
		}
	}
	return IsPreparationContextCurrent(OutContext);
}

bool URoomCombatSubsystem::IsPreparationContextCurrent(
	const FRoomCombatPreparationContext& Context) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
	return CanRunServerGameplay() && Runtime && Runtime->State == ERoomCombatState::Preparing
		&& Context.PreparationId.IsValid() && Runtime->PreparationId == Context.PreparationId
		&& Runtime->RegistrationId == Context.RoomRegistrationId
		&& Runtime->GameplayGeneration == Context.GameplayGeneration
		&& Runtime->PreparingAnchorPlayer == Context.AnchorPlayer;
}

bool URoomCombatSubsystem::CancelInitialDetectionPreparation(
	const FRoomCombatPreparationContext& Context)
{
	if (!IsPreparationContextCurrent(Context))
	{
		return false;
	}
	FinishBarrierClosure(Context.RoomTag, false);
	if (!IsPreparationContextCurrent(Context)) return false;
	ClearInitialDetectionJoin(Context.PreparationId);
	ClearForcedJoinProtection(Context.PreparationId);
	if (!IsPreparationContextCurrent(Context)) return false;
	FRoomCombatRuntime& Runtime = RoomRuntimes.FindChecked(Context.RoomTag);
	const bool bWasExitBlocked = Runtime.bExitBlockActive;
	const int32 PhaseIndex = Runtime.CurrentCombatPhaseIndex;
	// 시도부터 무효화한 뒤 자신이 확보한 점유만 해제한다. 뒤늦은 실패 통지는
	// 새 준비나 다른 Room 전투를 취소할 수 없으며, Wave/처치/완료 기록은 유지한다.
	Runtime.PreparationId.Invalidate();
	Runtime.PreparingAnchorPlayer.Reset();
	Runtime.State = ERoomCombatState::Dormant;
	Runtime.bExitBlockActive = Runtime.bPreparationExitBlockWasActive;
	Runtime.NextPreparationAllowedTimeSeconds = GetWorld()->GetTimeSeconds()
		+ FMath::Max(0.0f, CVarInitialDetectionRetrySeconds.GetValueOnGameThread());
	if (Runtime.bPreparationOwnsReservation)
	{
		ReleaseCombatRoom(Context.RoomTag);
	}
	Runtime.bPreparationOwnsReservation = false;
	// 주기 Timer를 만들지 않는다. 대기 시간 이후 새 유효 감지가 와야 다시 준비한다.
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Initial detection cancelled. Room=%s RetryAfter=%.2f Generation=%d"),
		*Context.RoomTag.ToString(), Runtime.NextPreparationAllowedTimeSeconds, Context.GameplayGeneration);
	if (bWasExitBlocked && !Runtime.bExitBlockActive)
	{
		BroadcastCombatEvent(Context.RoomTag, ERoomCombatEvent::Cancelled,
			PhaseIndex, Context.GameplayGeneration, Context.RoomRegistrationId);
	}
	return true;
}

bool URoomCombatSubsystem::CompleteInitialDetectionPreparation(
	const FRoomCombatPreparationContext& Context)
{
	// 이동 요청이 소유한 준비는 EQS 완료 경로만 확정할 수 있다. 명시적 API로 대기를 건너뛰지 않는다.
	if (InitialDetectionJoins.Contains(Context.PreparationId)) return false;
	// 명시적 준비 API 사용자는 위치를 검증한다. 실제 감지 경로는 아래 검증을 함께 전달한다.
	return CompleteInitialDetectionPreparation(Context, [] { return true; });
}

bool URoomCombatSubsystem::CompleteInitialDetectionPreparation(
	const FRoomCombatPreparationContext& Context, TFunction<bool()> IsJoinCurrent)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Context.RoomTag);
	// 준비 수명과 호출자가 보존한 합류 위치/방 수명을 모두 확인한 뒤 차단을 시작한다.
	if (!IsPreparationContextCurrent(Context) || !IsJoinCurrent() || IsRoomStartHeld(Context.RoomTag)
		|| !IsRoomCombatReserved(Context.RoomTag)
		|| !Runtime->RoomVolume.IsValid()
		|| !Runtime->PreparingAnchorPlayer.IsValid()
		|| Runtime->PreparingAnchorPlayer->IsActorBeingDestroyed())
	{
		return false;
	}

	CompactAliveEnemies(*Runtime);
	if (!IsInitialDetectionPhase(Context.RoomTag, *Runtime)
		|| (Runtime->TrackedAliveEnemies.IsEmpty() && !Runtime->bHadPreplacedEnemy))
	{
		return false;
	}
	// Anchor/같은 방(Moved=None)도 여기서 빠짐없이 검사한다. pending 중에는
	// Preparing 토큰과 합류 요청을 유지하고, 모든 캡슐 분리 후에만 아래 commit을 재개한다.
	const TWeakObjectPtr<URoomCombatSubsystem> WeakThis(this);
	if (!PrepareBarrierClosure(Context.RoomTag,
		[WeakThis, Context, IsJoinCurrent]
		{ return WeakThis.IsValid() && WeakThis->IsPreparationContextCurrent(Context) && IsJoinCurrent(); },
		[WeakThis, Context, IsJoinCurrent](bool bSafe)
		{
			if (!WeakThis.IsValid() || !WeakThis->IsPreparationContextCurrent(Context)) return;
			if (!bSafe || !WeakThis->CompleteInitialDetectionPreparation(Context, IsJoinCurrent))
			{
				WeakThis->CancelInitialDetectionPreparation(Context);
				return;
			}
			WeakThis->ClearInitialDetectionJoin(Context.PreparationId);
			const FRoomCombatRuntime* Current = WeakThis->RoomRuntimes.Find(Context.RoomTag);
			if (Current && Current->RegistrationId == Context.RoomRegistrationId
				&& Current->GameplayGeneration == Context.GameplayGeneration
				&& Current->State == ERoomCombatState::Combat && Context.AnchorPlayer.IsValid())
				if (UEnemyRoomSubsystem* Rooms = WeakThis->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
					Rooms->NotifyRoomCombat(Context.RoomTag, Context.AnchorPlayer->GetActorLocation(), nullptr);
		})) return false;
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Initial detection ready. Room=%s Player=%s Generation=%d"),
		*Context.RoomTag.ToString(), *GetNameSafe(Context.AnchorPlayer.Get()),
		Context.GameplayGeneration);
	// 차단막 이벤트를 먼저 반영한다. Wave 1의 생존 수가 0이면 시작과 동시에 후속 증원이 가능하다.
	Runtime->bExitBlockActive = true;
	BroadcastCombatEvent(Context.RoomTag, ERoomCombatEvent::SequenceStarted,
		Runtime->CurrentCombatPhaseIndex, Runtime->GameplayGeneration);
	// 이벤트 수신 중 Reset/언로드가 발생했으면 이전 준비 요청으로 Wave를 열지 않는다.
	Runtime = RoomRuntimes.Find(Context.RoomTag);
	if (!IsPreparationContextCurrent(Context) || !IsJoinCurrent() || IsRoomStartHeld(Context.RoomTag)
		|| !Runtime->PreparingAnchorPlayer.IsValid()
		|| Runtime->PreparingAnchorPlayer->IsActorBeingDestroyed()
		|| !Runtime->RoomVolume.IsValid()
		|| !IsRoomCombatReserved(Context.RoomTag))
	{
		return false;
	}
	StartInitialDetectionCombat(Context.RoomTag, *Runtime);
	return true;
}

bool URoomCombatSubsystem::IsEnemyAttackBlocked(AEnemyBase* Enemy) const
{
	// 빙의체와 이미 아군으로 제외된 배치는 적 Encounter의 합류 준비에 묶이지 않는다.
	if (!Enemy || Enemy->IsEnemyPossessed())
	{
		return false;
	}
	const FRoomCombatEnemyRegistration* Registration =
		RegisteredEnemies.Find(TWeakObjectPtr<AEnemyBase>(Enemy));
	if (Registration && Registration->bExcludedAsAlly)
	{
		return false;
	}
	const FRoomCombatRuntime* Runtime = Registration
		? RoomRuntimes.Find(Registration->RoomTag)
		: nullptr;
	return Registration && Registration->bPreplaced && Runtime
		&& (IsRoomStartHeld(Registration->RoomTag)
			|| (Runtime->State == ERoomCombatState::Preparing
				&& IsRoomCombatReserved(Registration->RoomTag)));
}

bool URoomCombatSubsystem::NotifyRoomCombatStarted(FGameplayTag RoomTag)
{
	// 기존 테스트/호환용 직접 시작 경로. 실제 감지 전파는 플레이어 합류 경로를 사용한다.
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag) || !RoomTag.IsValid())
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
		return IsRoomCombatReserved(RoomTag);
	}
	if (Runtime->State != ERoomCombatState::Dormant
		|| Runtime->bSavedCurrentPhasePending
		|| !CanReserveCombatRoom(RoomTag))
	{
		return false;
	}

	CompactAliveEnemies(*Runtime);
	if (Runtime->TrackedAliveEnemies.IsEmpty()
		|| !IsInitialDetectionPhase(RoomTag, *Runtime))
	{
		return false;
	}
	if (!PrepareBarrierClosure(RoomTag, [this, RoomTag]
		{ return CanRunServerGameplay() && GetRoomState(RoomTag) == ERoomCombatState::Dormant; }, [](bool) {})) return false;

	StartInitialDetectionCombat(RoomTag, *Runtime);
	return true;
}

const FRoomCombatWaveDefinition* URoomCombatSubsystem::FindSpawnableWave(
	FGameplayTag RoomTag,
	int32 CombatPhaseIndex,
	int32 WaveIndex) const
{
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag))
	{
		return nullptr;
	}

	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	if (!Runtime || !Definition || !IsActiveCombatRuntime(RoomTag, *Runtime)
		|| Runtime->CurrentCombatPhaseIndex != CombatPhaseIndex
		|| Runtime->bPhaseDelayPending)
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
	Runtime->bCurrentWaveHasSpawned = false;
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
		if (Turret->BeginRoomWaveDeployment(Runtime.GameplayGeneration))
		{
			UE_LOG(LogTemp, Display,
				TEXT("[RoomCombat] Wave turret deployment started. Turret=%s Room=%s Phase=%d Wave=%d Generation=%d Serial=%d PendingActivation=%d Alive=%d"),
				*GetNameSafe(Turret),
				*RoomTag.ToString(),
				Runtime.CurrentCombatPhaseIndex,
				Runtime.CurrentWaveIndex,
				Runtime.GameplayGeneration,
				Turret->GetRoomWaveActivationSerial(),
				Runtime.PendingActivationCount,
				Runtime.TrackedAliveEnemies.Num());
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
	NotifyFirstWaveSpawn(RoomTag, Runtime);
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
	// 소환 지연이 있으면 On/Loop가 먼저 들리고, 지연이 없으면 첫 Act와 겹친다.
	RefreshSpawnPointWorkStates(RoomTag, true);

	UE_LOG(LogTemp, Display,
		TEXT("[Reinforcement] Enemy=None Stage=WaveQueued Room=%s Phase=%d Wave=%d Generation=%d Requests=%d SpawnPoints=%d RequiredSpawnPointTag=%s"),
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
	NotifyFirstWaveSpawn(SpawnRequest.RoomTag, *Runtime);
	return true;
}

void URoomCombatSubsystem::NotifyFirstWaveSpawn(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime)
{
	if (Runtime.bCurrentWaveHasSpawned)
	{
		return;
	}
	Runtime.bCurrentWaveHasSpawned = true;
	const FGuid RegistrationId = Runtime.RegistrationId;
	const int32 Generation = Runtime.GameplayGeneration;
	const int32 PhaseIndex = Runtime.CurrentCombatPhaseIndex;
	const int32 WaveIndex = Runtime.CurrentWaveIndex;
	// Pool/터렛 등록의 호출 스택이 끝난 뒤 알린다. 수신자가 Reset하더라도 진행 중인
	// Pending 배열이나 Runtime 참조를 무효화하지 않으며, 리로드된 알림은 수명 토큰으로 버린다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, RoomTag, RegistrationId, Generation, PhaseIndex, WaveIndex]()
		{
			const FRoomCombatRuntime* Current = RoomRuntimes.Find(RoomTag);
			if (!CanRunServerGameplay() || !Current || Current->RegistrationId != RegistrationId
				|| Current->GameplayGeneration != Generation || Current->State != ERoomCombatState::Combat
				|| Current->CurrentCombatPhaseIndex != PhaseIndex)
			{
				return;
			}
			OnWaveFirstSpawned.Broadcast(RoomTag, PhaseIndex, WaveIndex, Generation, RegistrationId);
		}));
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
			TEXT("[Reinforcement] Enemy=%s Stage=SpawnRegistered Generation=%d Lease=%d Room=%s Phase=%d Wave=%d SpawnPoint=%s Location=%s Remaining=%d"),
			*GetNameSafe(Enemy), Request.GameplayGeneration, Enemy->GetPoolLeaseSerial(),
			*RoomTag.ToString(),
			Request.CombatPhaseIndex,
			Request.WaveIndex,
			*GetNameSafe(SpawnPoint),
			*SpawnTransform.GetLocation().ToCompactString(),
			GetPendingSpawnCount(RoomTag) - 1);

		SpawnPoint->PlaySpawnActAudio();
		Enemy->PlaySpawnAppearAudio();

		// Pool 대여와 Room 생존 집계 등록이 모두 성공해야 요청 하나가 완료된다.
		PendingSpawnRequests.RemoveAtSwap(RequestIndex, 1, EAllowShrinking::No);
	}

	// 마지막 배정분을 내보낸 지점은 Off, 재배정으로 새로 일을 받은 지점은 On.
	RefreshSpawnPointWorkStates(RoomTag, true);

	if (!HasPendingSpawns(RoomTag))
	{
		Runtime->SpawnRetryAttempts = 0;
		if (PendingSpawnRequests.IsEmpty()) CancelSpawnRetry();
		FinalizeCurrentWaveSpawn(RoomTag, *Runtime);
		return;
	}

	++Runtime->SpawnRetryAttempts;
	const double CurrentTimeSeconds = World->GetTimeSeconds();
	if (CurrentTimeSeconds - Runtime->LastSpawnRetryLogSeconds >= 5.0)
	{
		Runtime->LastSpawnRetryLogSeconds = CurrentTimeSeconds;
		UE_LOG(LogTemp, Warning,
			TEXT("[Reinforcement] Enemy=None Stage=WavePending Room=%s Phase=%d Wave=%d Pending=%d Attempts=%d MissingSpawnPoint=%d BlockedLocation=%d PoolLeaseFailure=%d RegistrationFailure=%d"),
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
	const UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	// 한 방의 소환 콜백이 다른 방을 완료/해제할 수 있다. 요청이 있는 방의 등록
	// 수명을 먼저 복사하고, 매 방 처리 직전에 다시 조회해 이전 포인터를 넘기지 않는다.
	TMap<FGameplayTag, FGuid> RetryRooms;
	for (const FRoomCombatPendingSpawn& Request : PendingSpawnRequests)
	{
		if (const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Request.RoomTag))
		{
			RetryRooms.Add(Request.RoomTag, Runtime->RegistrationId);
		}
	}
	for (const TPair<FGameplayTag, FGuid>& Entry : RetryRooms)
	{
		const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(Entry.Key);
		if (!Runtime || Runtime->RegistrationId != Entry.Value) continue;
		if (ArenaSubsystem && Runtime->GameplayGeneration != static_cast<int32>(ArenaSubsystem->GetGameplayGeneration()))
		{
			PendingSpawnRequests.RemoveAll([RoomTag = Entry.Key](const FRoomCombatPendingSpawn& Request)
			{
				return Request.RoomTag == RoomTag;
			});
			RefreshSpawnPointWorkStates(Entry.Key, false);
			continue;
		}
		if (IsActiveCombatRuntime(Entry.Key, *Runtime)) TrySpawnPendingRequests(Entry.Key);
	}
	if (PendingSpawnRequests.IsEmpty()) CancelSpawnRetry();
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
		TEXT("[Reinforcement] Enemy=None Stage=WaveCompleted Room=%s Phase=%d Wave=%d Baseline=%d"),
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

	// 합류 준비 중 배치 적이 전멸했다면 기준값은 0이지만 다음 Wave는 즉시 시작해야 한다.
	const bool bClearedPreplacedWaveDuringPreparation = Runtime.WaveBaselineEnemyCount == 0
		&& Runtime.CurrentWaveIndex == 0
		&& Wave->SpawnMode == ERoomCombatWaveSpawnMode::Preplaced
		&& Runtime.bHadPreplacedEnemy && AliveEnemyCount == 0;
	if (Runtime.WaveBaselineEnemyCount <= 0 && !bClearedPreplacedWaveDuringPreparation)
	{
		return;
	}

	// 분모는 소환 완료 때 확정한 값이다. 사망할 때는 분자만 줄이고 다음 증원 완료 때 갱신한다.
	const float RemainingRatio = bClearedPreplacedWaveDuringPreparation ? 0.0f
		: static_cast<float>(AliveEnemyCount) / static_cast<float>(Runtime.WaveBaselineEnemyCount);
	const float RequiredRatio = Wave->NextWaveRemainingRatio;
	if (RemainingRatio > RequiredRatio)
	{
		return;
	}

	const int32 PhaseIndex = Runtime.CurrentCombatPhaseIndex;
	const int32 NextWaveIndex = Runtime.CurrentWaveIndex + 1;
	UE_LOG(LogTemp, Display,
		TEXT("[Reinforcement] Enemy=None Stage=WaveTriggered Room=%s Phase=%d CurrentWave=%d NextWave=%d Alive=%d Baseline=%d Ratio=%.3f Required=%.3f"),
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
			TEXT("[Reinforcement] Enemy=None Stage=WaveStartFailed Room=%s Phase=%d Wave=%d Alive=%d Baseline=%d Ratio=%.3f Required=%.3f"),
			*RoomTag.ToString(),
			Runtime.CurrentCombatPhaseIndex,
			NextWaveIndex,
			AliveEnemyCount,
			Runtime.WaveBaselineEnemyCount,
			RemainingRatio,
			RequiredRatio);
		return;
	}
	// 즉시 소환이 다음 Wave/차수 전환까지 이어질 수 있으므로 Phase는 시작 요청 시점 값으로 보낸다.
	OnReinforcementStarted.Broadcast(RoomTag, PhaseIndex, NextWaveIndex, Runtime.GameplayGeneration);
}

void URoomCombatSubsystem::ResetRuntimeCombatState()
{
	if (bResettingRuntime)
	{
		return;
	}
	// 취소 통보에서 Reset이 재진입하거나 새 전투를 시작하지 못하게 정리 전체를 하나의 경계로 묶는다.
	TGuardValue<bool> ResetGuard(bResettingRuntime, true);
	TArray<FGameplayTag> PendingBarrierRooms;
	BarrierClosurePreparations.GetKeys(PendingBarrierRooms);
	for (FGameplayTag Tag : PendingBarrierRooms) FinishBarrierClosure(Tag, false);
	TArray<FGuid> JoinIds;
	InitialDetectionJoins.GetKeys(JoinIds);
	for (FGuid Id : JoinIds) ClearInitialDetectionJoin(Id);
	TArray<FGuid> ProtectionIds;
	ForcedJoinProtections.GetKeys(ProtectionIds);
	for (FGuid Id : ProtectionIds) ClearForcedJoinProtection(Id);
	++MovementRoomRevision;
	RoomStartHolds.Reset();
	bRestoredAutomaticStartAllowed = false;
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Runtime reset started. Rooms=%d Enemies=%d WaveTurrets=%d PendingSpawns=%d ActiveRoom=%s"),
		RoomRuntimes.Num(), RegisteredEnemies.Num(), RegisteredWaveTurretRooms.Num(),
		PendingSpawnRequests.Num(), *ActiveCombatRoomTag.ToString());
	struct FCancelledSequence
	{
		FGameplayTag RoomTag;
		int32 PhaseIndex;
		int32 Generation;
		FGuid RegistrationId;
		FGuid ExecutionId;
	};
	TArray<FCancelledSequence> CancelledSequences;
	TArray<TWeakObjectPtr<AEnemyBase>> EnemiesToReturn;
	TArray<TWeakObjectPtr<AAutoTurret>> WaveTurretsToReset;
	for (const auto& Entry : RegisteredEnemies)
	{
		if (Entry.Value.bPoolManaged)
		{
			EnemiesToReturn.Add(Entry.Key);
		}
	}
	RegisteredWaveTurretRooms.GetKeys(WaveTurretsToReset);
	CancelSpawnRetry();
	for (TPair<FGameplayTag, FRoomCombatRuntime>& Entry : RoomRuntimes)
	{
		GetWorld()->GetTimerManager().ClearTimer(Entry.Value.InitialSpawnDelayTimer);
		GetWorld()->GetTimerManager().ClearTimer(Entry.Value.PhaseDelayTimer);
		SetActivationGroupActive(Entry.Key, Entry.Value.ActiveActivationGroupTag, false);
		// 준비 중에는 차단막이 없어도 대기 중인 합류 요청에 취소를 알려야 한다.
		if (Entry.Value.bExitBlockActive || Entry.Value.bTriggeredSequenceActive
			|| Entry.Value.CheckpointHoldExecutionId.IsValid()
			|| Entry.Value.State == ERoomCombatState::Preparing)
		{
			CancelledSequences.Add({Entry.Key, Entry.Value.CurrentCombatPhaseIndex, Entry.Value.GameplayGeneration,
				Entry.Value.RegistrationId, Entry.Value.CheckpointHoldExecutionId});
		}
	}
	// 토큰을 먼저 무효화해야 Reset 도중 도착한 AnimNotify가 현재 Wave의 Pending을 소모하지 못한다.
	for (const TWeakObjectPtr<AAutoTurret>& TurretPtr : WaveTurretsToReset)
	{
		if (AAutoTurret* Turret = TurretPtr.Get())
		{
			Turret->ResetRoomWaveLifecycle(TEXT("Arena gameplay reset"));
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
	PublishCombatMusicState(false);
	PendingPreplacedEnemies.Reset();
	RegisteredEnemies.Reset();
	PendingSpawnRequests.Reset();
	for (const TPair<FGameplayTag, TArray<FRoomCombatSpawnPointRuntime>>& Entry : SpawnPointsByRoom)
	{
		RefreshSpawnPointWorkStates(Entry.Key, false);
	}
	SpawnPointsByRoom.Reset();
	RegisteredSpawnPointRooms.Reset();
	WaveTurretsByRoom.Reset();
	RegisteredWaveTurretRooms.Reset();
	if (UEnemyRoomSubsystem* EnemyRooms = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>() : nullptr)
	{
		for (FGameplayTag RoomTag : ReservedCombatRooms)
		{
			EnemyRooms->NotifyRoomCombatEnded(RoomTag);
		}
	}
	ReservedCombatRooms.Reset();
	ActiveCombatGroupId.Invalidate();
	ActiveCombatGroupTag = FGameplayTag();
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
			Sequence.PhaseIndex, Sequence.Generation, Sequence.RegistrationId, Sequence.ExecutionId);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Runtime reset completed. Rooms=%d Enemies=%d WaveTurrets=%d PendingSpawns=%d"),
		RoomRuntimes.Num(), RegisteredEnemies.Num(), RegisteredWaveTurretRooms.Num(),
		PendingSpawnRequests.Num());
}

void URoomCombatSubsystem::ResetWaveTurretsForRoom(FGameplayTag RoomTag, const TCHAR* ResetReason)
{
	const TArray<FRoomCombatWaveTurretRuntime>* Entries = WaveTurretsByRoom.Find(RoomTag);
	if (!Entries)
	{
		return;
	}

	for (const FRoomCombatWaveTurretRuntime& Entry : *Entries)
	{
		if (AAutoTurret* Turret = Entry.Turret.Get())
		{
			Turret->ResetRoomWaveLifecycle(ResetReason);
		}
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

ARoomVolume* URoomCombatSubsystem::GetRegisteredRoomVolume(FGameplayTag RoomTag) const
{
	const FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	return CanRunServerGameplay() && Runtime ? Runtime->RoomVolume.Get() : nullptr;
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

	int32 MatchingTurretCount = 0;
	for (const FRoomCombatWaveTurretRuntime& Entry : *Entries)
	{
		if (Entry.CombatPhaseIndex == CombatPhaseIndex && Entry.WaveIndex == WaveIndex)
		{
			++MatchingTurretCount;
		}
	}
	return MatchingTurretCount;
}

void URoomCombatSubsystem::CompleteCurrentPhase(
	FGameplayTag RoomTag,
	bool bCancelRemainingWaves,
	bool bFinishRoom)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* CurrentPhase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex)
		: nullptr;
	if ((bCheckpointCleanupInProgress && !bFinishRoom)
		|| !Runtime || !Definition || !CurrentPhase || Runtime->bPhaseDelayPending || Runtime->bPhaseCleanupInProgress
		|| Runtime->State == ERoomCombatState::Cleared
		|| (!bFinishRoom && Runtime->State == ERoomCombatState::WaitingForTrigger))
	{
		return;
	}
	const int32 CompletedPhaseIndex = Runtime->CurrentCombatPhaseIndex;
	const int32 Generation = Runtime->GameplayGeneration;
	const FGuid RegistrationId = Runtime->RegistrationId;
	const FGuid ExecutionId = Runtime->CheckpointHoldExecutionId;
	const int32 NextPhaseIndex = CompletedPhaseIndex + 1;
	const FRoomCombatPhaseDefinition* NextPhase = bFinishRoom ? nullptr : Definition->FindPhase(NextPhaseIndex);
	// 옵션은 다음 외부 이벤트 앞에서만 유효하다. 잘못된 조합은 기존 봉쇄 정책을 유지한다.
	const bool bReleaseCombat = CurrentPhase->bReleaseCombatOnCompletion && NextPhase
		&& NextPhase->StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger;
	// 차수가 완료되면 이전 합류 기준 플레이어를 다음 차수로 넘기지 않는다.
	Runtime->PreparingAnchorPlayer.Reset();
	Runtime->PreparationId.Invalidate();
	Runtime->bInitialDetectionPendingJoin = false;
	Runtime->WaveBaselineEnemyCount = INDEX_NONE;
	Runtime->PendingActivationCount = 0;
	Runtime->PendingWaveTurretActivations.Reset();
	Runtime->bCurrentWaveSpawnStarted = false;
	Runtime->bCurrentWaveHasSpawned = false;
	Runtime->SpawnAssignments.Reset();

	const bool bContinueAutomatically = Runtime->bTriggeredSequenceActive && NextPhase
		&& NextPhase->StartPolicy == ERoomCombatPhaseStartPolicy::Automatic;
	if (!bContinueAutomatically)
	{
		// 일반 외부 대기는 점유·봉쇄를 유지한다. 명시한 휴식 경계만 현재 Room의 전투를 끝낸다.
		if (bReleaseCombat)
		{
			Runtime->bExitBlockActive = false;
		}
		const bool bKeepRoomReserved = Runtime->bExitBlockActive && NextPhase != nullptr;
		if (!bKeepRoomReserved)
		{
			ReleaseCombatRoom(RoomTag);
		}
		if (UWorld* World = GetWorld())
		{
			if (UEnemyRoomSubsystem* EnemyRoomSubsystem = World->GetSubsystem<UEnemyRoomSubsystem>())
			{
				EnemyRoomSubsystem->NotifyRoomCombatEnded(RoomTag);
			}
		}
		Runtime->bTriggeredSequenceActive = false;
		SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, false);
	}

	if (!NextPhase)
	{
		Runtime->bExitBlockActive = false;
		MarkRoomCleared(RoomTag, *Runtime);
		if (UWorld* World = GetWorld())
		{
			if (UOutlierSaveSubSystem* Saves = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr)
			{
				Saves->ClearCurrentRoomPhaseProgress(RoomTag);
			}
			if (AOutlierGameMode* GameMode = World->GetAuthGameMode<AOutlierGameMode>();
				GameMode && !bCheckpointCleanupInProgress)
			{
				GameMode->CommitCombatPhaseCheckpoint(RoomTag, INDEX_NONE, true, Generation);
			}
		}
		// 전체 완료에서 차단 해제를 먼저 알린다. 이어지는 차수 알림이 Reset을 요청해도 해제가 누락되지 않는다.
		BroadcastCombatEvent(RoomTag, ERoomCombatEvent::RoomCleared, CompletedPhaseIndex, Generation,
			RegistrationId, ExecutionId);
		// 완료 이벤트 수신 중 Room 해제/Reset이 일어나면 이전 Room의 후속 통보를 보내지 않는다.
		Runtime = RoomRuntimes.Find(RoomTag);
		if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->State == ERoomCombatState::Cleared)
		{
			BroadcastCombatEvent(RoomTag, ERoomCombatEvent::PhaseCompleted, CompletedPhaseIndex, Generation,
				RegistrationId, ExecutionId);
		}
		return;
	}

	// 완료 → 다음 차수/대기 확정 → 경계 저장 → 완료 통보 → 딜레이 종료 후 시작.
	// 자동/외부 차수 모두 이 경로를 사용하며, 대기 중에는 다음 Wave 명단을 만들지 않는다.
	Runtime->CurrentCombatPhaseIndex = NextPhaseIndex;
	Runtime->CurrentWaveIndex = 0;
	if (!bContinueAutomatically)
	{
		const bool bWaitForTrigger = NextPhase->StartPolicy == ERoomCombatPhaseStartPolicy::HackTrigger
			|| NextPhase->StartPolicy == ERoomCombatPhaseStartPolicy::ExternalTrigger;
		Runtime->State = bWaitForTrigger ? ERoomCombatState::WaitingForTrigger : ERoomCombatState::Dormant;
	}
	BeginPhaseDelay(RoomTag, *Runtime);
	RecordPhaseBoundary(RoomTag, *Runtime);
	Runtime = RoomRuntimes.Find(RoomTag);
	if (!Runtime || Runtime->RegistrationId != RegistrationId)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombat] Phase completed. RoomTag=%s Phase=%d CancelledRemainingWaves=%d NextPhase=%d State=%d"),
		*RoomTag.ToString(),
		CompletedPhaseIndex,
		bCancelRemainingWaves ? 1 : 0,
		NextPhaseIndex,
		static_cast<int32>(Runtime->State));
	BroadcastCombatEvent(RoomTag, ERoomCombatEvent::PhaseCompleted, CompletedPhaseIndex, Generation,
		RegistrationId, ExecutionId);
	// 0초 자동 차수도 완료 이벤트 처리가 끝난 뒤 시작한다. 이벤트 중 Reset됐다면 토큰 검증에서 거부된다.
	if (GetPhaseDelayRemainingSeconds(RoomTag) == 0.0f)
	{
		CompletePhaseDelay(RoomTag, RegistrationId, Generation, NextPhaseIndex);
	}
}

void URoomCombatSubsystem::BeginPhaseDelay(FGameplayTag RoomTag, FRoomCombatRuntime& Runtime)
{
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Definition
		? Definition->FindPhase(Runtime.CurrentCombatPhaseIndex) : nullptr;
	Runtime.bPhaseDelayPending = true;
	if (!Phase || !FMath::IsFinite(Phase->DelayBeforeStartSeconds) || Phase->DelayBeforeStartSeconds < 0.0f)
	{
		// 잘못된 값은 즉시 전환으로 통과시키지 않는다. 차단을 유지하고 데이터 오류를 진단한다.
		UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Invalid phase delay. Room=%s Phase=%d"),
			*RoomTag.ToString(), Runtime.CurrentCombatPhaseIndex);
		return;
	}
	const int32 PhaseIndex = Runtime.CurrentCombatPhaseIndex;
	const int32 Generation = Runtime.GameplayGeneration;
	const FGuid RegistrationId = Runtime.RegistrationId;
	// 0초 외부 차수는 완료 이벤트 안에서 바로 시작할 수 있다. 자동 차수만 통보 뒤에 재개한다.
	if (Phase->DelayBeforeStartSeconds == 0.0f && Phase->StartPolicy != ERoomCombatPhaseStartPolicy::Automatic)
	{
		Runtime.bPhaseDelayPending = false;
	}
	if (Phase->DelayBeforeStartSeconds > 0.0f)
	{
		// World 타이머이므로 GameOver pause 중에는 남은 시간도 멈춘다.
		GetWorld()->GetTimerManager().SetTimer(Runtime.PhaseDelayTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, RoomTag, RegistrationId, Generation, PhaseIndex]()
			{
				CompletePhaseDelay(RoomTag, RegistrationId, Generation, PhaseIndex);
			}), Phase->DelayBeforeStartSeconds, false);
	}
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Phase delay started. Room=%s Phase=%d Generation=%d Seconds=%.3f"),
		*RoomTag.ToString(), PhaseIndex, Generation, Phase->DelayBeforeStartSeconds);
}

void URoomCombatSubsystem::CompletePhaseDelay(
	FGameplayTag RoomTag, FGuid RegistrationId, int32 Generation, int32 PhaseIndex)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	// 1. 콜백을 예약한 Room 등록/차수 수명이 아직 유효한지 확인한다.
	if (!CanRunServerGameplay() || !Runtime || !Runtime->RoomVolume.IsValid()
		|| !Runtime->bPhaseDelayPending || Runtime->RegistrationId != RegistrationId
		|| Runtime->GameplayGeneration != Generation || Runtime->CurrentCombatPhaseIndex != PhaseIndex)
	{
		return;
	}
	const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (Arena && static_cast<int32>(Arena->GetGameplayGeneration()) != Generation)
	{
		return;
	}

	// 2. 잘못된 설정이나 대기를 끝낼 수 없는 상태를 정상 만료로 통과시키지 않는다.
	const FRoomCombatRoomDefinition* Definition = FindRoomDefinition(RoomTag);
	const FRoomCombatPhaseDefinition* Phase = Definition ? Definition->FindPhase(PhaseIndex) : nullptr;
	if (!Phase || !FMath::IsFinite(Phase->DelayBeforeStartSeconds) || Phase->DelayBeforeStartSeconds < 0.0f)
	{
		return;
	}
	const bool bCanFinishDelay = Runtime->State == ERoomCombatState::Combat
		|| Runtime->State == ERoomCombatState::WaitingForTrigger
		|| Runtime->State == ERoomCombatState::Dormant;
	if (!bCanFinishDelay)
	{
		return;
	}

	// 3. 대기를 해제한 뒤 자동 차수는 시작하고, 외부 차수는 새 트리거를 기다린다.
	Runtime->bPhaseDelayPending = false;
	GetWorld()->GetTimerManager().ClearTimer(Runtime->PhaseDelayTimer);
	UE_LOG(LogTemp, Display, TEXT("[RoomCombat] Phase delay finished. Room=%s Phase=%d Generation=%d"),
		*RoomTag.ToString(), PhaseIndex, Generation);
	if (Phase->StartPolicy == ERoomCombatPhaseStartPolicy::Automatic)
	{
		StartAutomaticPhase(RoomTag);
	}
	else
	{
		// 외부 차수는 시작 가능 상태만 알린다. 조기 요청은 보관하지 않았으므로 새 요청이 필요하다.
		OnRoomStartReadinessChanged.Broadcast(RoomTag);
	}
}

void URoomCombatSubsystem::StartAutomaticPhase(FGameplayTag RoomTag)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* Phase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex) : nullptr;
	if (!CanRunServerGameplay() || IsRoomStartHeld(RoomTag)
		|| !Runtime || Runtime->State != ERoomCombatState::Combat || Runtime->bPhaseDelayPending
		|| !Phase || Phase->StartPolicy != ERoomCombatPhaseStartPolicy::Automatic)
	{
		return;
	}
	const FGuid RegistrationId = Runtime->RegistrationId;
	const int32 PhaseIndex = Runtime->CurrentCombatPhaseIndex;
	const int32 Generation = Runtime->GameplayGeneration;
	Runtime->bDeferSpawnExecution = true;
	if (!StartWaveSpawning(RoomTag, PhaseIndex, 0))
	{
		Runtime->bDeferSpawnExecution = false;
		UE_LOG(LogTemp, Error, TEXT("[RoomCombat] Automatic phase failed. Room=%s Phase=%d Generation=%d"),
			*RoomTag.ToString(), PhaseIndex, Generation);
		return;
	}
	// 시작 통보에서 Reset될 수 있다. 콜백 뒤에는 기존 Runtime 참조를 재사용하지 않는다.
	BroadcastCombatEvent(RoomTag, ERoomCombatEvent::SequenceStarted, PhaseIndex, Generation);
	ResumeDeferredSpawning(RoomTag, RegistrationId);
}

void URoomCombatSubsystem::CaptureRoomPhaseProgressForCheckpoint(
	TMap<FGameplayTag, FOutlierRoomPhaseProgress>& OutProgress) const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UOutlierSaveSubSystem* Saves = GameInstance ? GameInstance->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	OutProgress = Saves ? Saves->GetCurrentRoomPhaseProgress() : TMap<FGameplayTag, FOutlierRoomPhaseProgress>();
	// A의 완료 경계에서도 B는 전투 중일 수 있다. 완료 경계 기록을 보존한 뒤
	// 실제 참여 방만 덮어써 B의 첫 차수와 수락된 문/해킹 전투까지 함께 복원한다.
	for (const auto& Entry : RoomRuntimes)
	{
		const FRoomCombatRuntime& Runtime = Entry.Value;
		if (!Runtime.bExitBlockActive || !IsRoomCombatReserved(Entry.Key) || !Runtime.RoomVolume.IsValid()) continue;
		FOutlierRoomPhaseProgress& Progress = OutProgress.FindOrAdd(Entry.Key);
		Progress.NextPhaseIndex = Runtime.CurrentCombatPhaseIndex;
		Progress.bExitBlockActive = true;
		Progress.bTriggeredSequenceActive = Runtime.bTriggeredSequenceActive;
		Progress.ActivationGroupTag = Runtime.ActiveActivationGroupTag;
		Progress.bResumeCurrentPhase = Runtime.bSavedCurrentPhasePending
			|| (Runtime.State == ERoomCombatState::Combat && !Runtime.bPhaseDelayPending
				&& !Runtime.bSavedAutomaticPhasePending);
	}
}

void URoomCombatSubsystem::RecordPhaseBoundary(
	FGameplayTag RoomTag, const FRoomCombatRuntime& Runtime)
{
	UWorld* World = GetWorld();
	UOutlierSaveSubSystem* Saves = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	if (!Saves)
	{
		return;
	}
	FOutlierRoomPhaseProgress Progress;
	Progress.NextPhaseIndex = Runtime.CurrentCombatPhaseIndex;
	Progress.bExitBlockActive = Runtime.bExitBlockActive;
	Progress.bTriggeredSequenceActive = Runtime.bTriggeredSequenceActive;
	Progress.ActivationGroupTag = Runtime.ActiveActivationGroupTag;
	Saves->SetCurrentRoomPhaseProgress(RoomTag, Progress);
	if (AOutlierGameMode* GameMode = World->GetAuthGameMode<AOutlierGameMode>())
	{
		GameMode->CommitCombatPhaseCheckpoint(
			RoomTag, Progress.NextPhaseIndex, false, Runtime.GameplayGeneration);
	}
}

void URoomCombatSubsystem::ResumeRestoredAutomaticPhases()
{
	if (!CanRunServerGameplay())
	{
		return;
	}
	bRestoredAutomaticStartAllowed = true;
	TArray<FGameplayTag> RoomTags;
	RoomRuntimes.GetKeys(RoomTags);
	for (FGameplayTag RoomTag : RoomTags)
	{
		TryStartRestoredAutomaticPhase(RoomTag);
	}
}

void URoomCombatSubsystem::TryStartRestoredAutomaticPhase(FGameplayTag RoomTag)
{
	FRoomCombatRuntime* Runtime = RoomRuntimes.Find(RoomTag);
	const FRoomCombatRoomDefinition* Definition = Runtime ? FindRoomDefinition(RoomTag) : nullptr;
	const FRoomCombatPhaseDefinition* Phase = Runtime && Definition
		? Definition->FindPhase(Runtime->CurrentCombatPhaseIndex) : nullptr;
	const UOutlierArenaSubsystem* Arena = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>() : nullptr;
	if (!bRestoredAutomaticStartAllowed || !CanRunServerGameplay() || IsRoomStartHeld(RoomTag)
		|| !Arena || !Arena->IsGameplayLevelsReady()
		|| !Runtime || (!Runtime->bSavedAutomaticPhasePending && !Runtime->bSavedCurrentPhasePending)
		|| Runtime->State != ERoomCombatState::Dormant
		|| !Phase
		|| !CanReserveCombatRoom(RoomTag))
	{
		return;
	}

	// 저장된 자동/진행 중 차수도 새 Wave를 확정하기 전에 출입구 안전 분리를 거친다.
	// 대기 중에는 복원 플래그를 소비하지 않아 실패/취소 후에도 재시도가 가능하다.
	const FGuid SafetyRegistration = Runtime->RegistrationId;
	const int32 SafetyGeneration = Runtime->GameplayGeneration;
	const int32 SafetyPhase = Runtime->CurrentCombatPhaseIndex;
	const TWeakObjectPtr<URoomCombatSubsystem> WeakThis(this);
	if (!PrepareBarrierClosure(RoomTag, [WeakThis, RoomTag, SafetyRegistration, SafetyGeneration, SafetyPhase]
	{
		const FRoomCombatRuntime* Current = WeakThis.IsValid() ? WeakThis->RoomRuntimes.Find(RoomTag) : nullptr;
		return Current && Current->RegistrationId == SafetyRegistration && Current->GameplayGeneration == SafetyGeneration
			&& Current->CurrentCombatPhaseIndex == SafetyPhase && Current->State == ERoomCombatState::Dormant
			&& (Current->bSavedAutomaticPhasePending || Current->bSavedCurrentPhasePending)
			&& WeakThis->bRestoredAutomaticStartAllowed && !WeakThis->IsRoomStartHeld(RoomTag);
	}, [WeakThis, RoomTag](bool bSafe)
	{
		if (bSafe && WeakThis.IsValid()) WeakThis->TryStartRestoredAutomaticPhase(RoomTag);
	})) return;

	// 이미 시작됐던 차수는 트리거를 다시 요구하지 않는다. 완료 경계의 후속
	// 차수와 구분하여 첫 Wave만 재구성하고, 문/해킹 오브젝트 이벤트는 재생하지 않는다.
	if (Runtime->bSavedCurrentPhasePending)
	{
		if (Phase->StartPolicy == ERoomCombatPhaseStartPolicy::InitialDetection)
		{
			if (Runtime->TrackedAliveEnemies.IsEmpty()) return;
			Runtime->bSavedCurrentPhasePending = false;
			Runtime->bSavedAutomaticPhasePending = false;
			const FGuid RegistrationId = Runtime->RegistrationId;
			StartInitialDetectionCombat(RoomTag, *Runtime);
			Runtime = RoomRuntimes.Find(RoomTag);
			if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->State == ERoomCombatState::Combat)
			{
				BroadcastCombatEvent(RoomTag, ERoomCombatEvent::SequenceStarted,
					Runtime->CurrentCombatPhaseIndex, Runtime->GameplayGeneration);
				Runtime = RoomRuntimes.Find(RoomTag);
				if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->State == ERoomCombatState::Combat)
				{
					if (UEnemyRoomSubsystem* EnemyRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
					{
						EnemyRooms->NotifyRoomCombat(RoomTag, Runtime->RoomVolume->GetActorLocation(), nullptr);
					}
				}
			}
			return;
		}
		Runtime->State = ERoomCombatState::Combat;
		Runtime->bDeferSpawnExecution = true;
		SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, true);
		if (!StartWaveSpawning(RoomTag, Runtime->CurrentCombatPhaseIndex, 0))
		{
			Runtime->State = ERoomCombatState::Dormant;
			Runtime->bDeferSpawnExecution = false;
			return;
		}
		Runtime->bSavedCurrentPhasePending = false;
		Runtime->bSavedAutomaticPhasePending = false;
		const FGuid RegistrationId = Runtime->RegistrationId;
		BroadcastCombatEvent(RoomTag, ERoomCombatEvent::SequenceStarted,
			Runtime->CurrentCombatPhaseIndex, Runtime->GameplayGeneration);
		Runtime = RoomRuntimes.Find(RoomTag);
		if (Runtime && Runtime->RegistrationId == RegistrationId && Runtime->State == ERoomCombatState::Combat)
		{
			// 새 Pool 대여가 이전 Room의 AI 공유 상태를 물려받지 않도록 소환 전에 전투를 연다.
			if (UEnemyRoomSubsystem* EnemyRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			{
				EnemyRooms->NotifyRoomCombat(RoomTag, Runtime->RoomVolume->GetActorLocation(), nullptr);
			}
		}
		ResumeDeferredSpawning(RoomTag, RegistrationId);
		return;
	}

	Runtime->State = ERoomCombatState::Combat;
	ReserveCombatRoom(RoomTag);
	SetActivationGroupActive(RoomTag, Runtime->ActiveActivationGroupTag, true);
	// Room 등록과 양쪽 준비가 끝난 뒤 딜레이를 새로 시작한다. 이전 완료 이벤트는 재발행하지 않는다.
	Runtime->bSavedAutomaticPhasePending = false;
	const FGuid RegistrationId = Runtime->RegistrationId;
	const int32 Generation = Runtime->GameplayGeneration;
	const int32 PhaseIndex = Runtime->CurrentCombatPhaseIndex;
	BeginPhaseDelay(RoomTag, *Runtime);
	if (GetPhaseDelayRemainingSeconds(RoomTag) == 0.0f)
	{
		CompletePhaseDelay(RoomTag, RegistrationId, Generation, PhaseIndex);
	}
}

void URoomCombatSubsystem::MarkRoomCleared(
	FGameplayTag RoomTag,
	FRoomCombatRuntime& Runtime)
{
	Runtime.State = ERoomCombatState::Cleared;
	ApplyRoomClearedToSpawnPoints(RoomTag, true);
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

void URoomCombatSubsystem::ApplyRoomClearedToSpawnPoints(FGameplayTag RoomTag, bool bCleared)
{
	CompactSpawnPoints(RoomTag);
	if (const TArray<FRoomCombatSpawnPointRuntime>* Points = SpawnPointsByRoom.Find(RoomTag))
	{
		for (const FRoomCombatSpawnPointRuntime& Entry : *Points)
		{
			if (ARoomCombatSpawnPoint* Point = Entry.SpawnPoint.Get())
			{
				Point->SetRoomCleared(bCleared);
			}
		}
	}
}

void URoomCombatSubsystem::RefreshSpawnPointWorkStates(FGameplayTag RoomTag, bool bPlayTransitionAudio)
{
	const TArray<FRoomCombatSpawnPointRuntime>* Points = SpawnPointsByRoom.Find(RoomTag);
	if (!Points)
	{
		return;
	}

	TSet<const ARoomCombatSpawnPoint*> WorkingPoints;
	for (const FRoomCombatPendingSpawn& Request : PendingSpawnRequests)
	{
		if (Request.RoomTag == RoomTag)
		{
			if (const ARoomCombatSpawnPoint* AssignedPoint = Request.AssignedSpawnPoint.Get())
			{
				WorkingPoints.Add(AssignedPoint);
			}
		}
	}

	for (const FRoomCombatSpawnPointRuntime& Entry : *Points)
	{
		if (ARoomCombatSpawnPoint* Point = Entry.SpawnPoint.Get())
		{
			Point->SetSpawnerWorking(WorkingPoints.Contains(Point), bPlayTransitionAudio);
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

			// EndPlay 해제를 받지 못한 경우에도 만료된 Actor의 역방향 인덱스를 함께 정리한다.
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

			// EndPlay 해제를 받지 못한 Actor도 역방향 인덱스에서 함께 제거한다.
			RegisteredWaveTurretRooms.Remove(Entry.Turret);
			return true;
		});

	if (Entries->IsEmpty())
	{
		WaveTurretsByRoom.Remove(RoomTag);
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

bool URoomCombatSubsystem::IsCombatMusicActive() const
{
 for (const auto& Pair : RoomRuntimes)
  if (Pair.Value.State == ERoomCombatState::Combat
   || (Pair.Value.State == ERoomCombatState::Preparing && Pair.Value.bExitBlockActive)) return true;
 return false;
}

void URoomCombatSubsystem::PublishCombatMusicState(bool bPlayEnd)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;
	const AGameStateBase* GameState = World->GetGameState();
	if (!GameState) return;
	const bool bActive = IsCombatMusicActive();
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		SendCombatMusicToPlayer(PlayerState, bActive, bPlayEnd);
	}
}

void URoomCombatSubsystem::SendCombatMusicToPlayer(
	APlayerState* PlayerState, bool bActive, bool bPlayEnd)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	APlayerController* PC = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
	UOutlierAudioSubsystem* Audio = GI ? GI->GetSubsystem<UOutlierAudioSubsystem>() : nullptr;
	if (!Audio) return;

	FOutlierAudioPlayRequest Request;
	Request.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM"));
	Request.ContextTags.AddTag(FGameplayTag::RequestGameplayTag(bActive
		? TEXT("Audio.Context.Bgm.inbattle.Start")
		: bPlayEnd ? TEXT("Audio.Context.Bgm.inbattle.End")
		: TEXT("Audio.Context.Bgm.inbattle.Stop")));
	Request.EmitterActor = PC;
	Request.RecipientActor = PC;
	Audio->PlayOwner2DFromServer(Request);
}
