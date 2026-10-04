#include "Interaction/Level1SuitUpgradeDoor.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/InteractableDoor.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Save/OutlierCheckpoint.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"

// Level 1 진행의 서버 측 순서:
// 두 플레이어가 입장 Room 안에 모임 -> 이 문 닫기 -> 닫힘 Timeline 완료
// -> 양쪽 UI의 실제 종료 확인 -> 이 문 열기 -> 열림 Timeline 완료
// -> 입구 체크포인트의 디스크 저장 성공 -> 전투 Room/SpawnPoint 준비 후 ExternalTrigger 시작.
ALevel1SuitUpgradeDoor::ALevel1SuitUpgradeDoor()
{
	// 부모 Door의 Tick이 Timeline을 구동한다. 자식은 초기 문 상태만 바꾼다.
	bInitiallyOpen = true;
}

void ALevel1SuitUpgradeDoor::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}

	if (!IsValid(TargetRoomVolume) || !TargetRoomVolume->GetRoomTag().IsValid()
		|| !IsValid(CombatRoomVolume) || !CombatRoomVolume->GetRoomTag().IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Invalid room setup. Door=%s EntryRoom=%s CombatRoom=%s"),
			*GetNameSafe(this), IsValid(TargetRoomVolume)
				? *TargetRoomVolume->GetRoomTag().ToString() : TEXT("None"),
			IsValid(CombatRoomVolume)
				? *CombatRoomVolume->GetRoomTag().ToString() : TEXT("None"));
		return;
	}
	if (!IsValid(EntranceCheckpoint))
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Entrance checkpoint missing. Door=%s"), *GetNameSafe(this));
		return;
	}
	if (!EntranceCheckpoint->IsCheckpointCommitted())
	{
		EntranceCheckpoint->SetActivationConditionSatisfied(nullptr, false);
	}

	UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Arena)
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Arena missing. Door=%s"), *GetNameSafe(this));
		return;
	}

	GameplayGeneration = Arena->GetGameplayGeneration();
	ArenaSubsystem = Arena;
	// PlayerState 구독 중 EvaluateEntry가 호출되므로, 그 전에 리로드 대기 상태를 정한다.
	bAwaitingGameplayReady = Arena->GetGameplayReloadPhase() != EOutlierGameplayReloadPhase::Ready;
	CombatSubsystem = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	if (!CombatSubsystem.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] RoomCombat missing. Door=%s"), *GetNameSafe(this));
		return;
	}
	// RoomVolume은 입장, PlayerState는 UI 완료, 부모 Door는 연출 완료를 각각 알린다.
	// 이 자식은 순서만 조합하고 메시/Timeline은 부모 구현을 그대로 사용한다.
	Arena->OnArenaGameplayReloadStarted.AddUObject(this, &ThisClass::OnArenaReloadStarted);
	Arena->OnArenaGameplayReady.AddUObject(this, &ThisClass::OnArenaGameplayReady);
	CombatSubsystem->OnRoomStartReadinessChanged.AddUObject(
		this, &ThisClass::OnRoomStartReadinessChanged);
	CombatSubsystem->OnCombatEvent.AddDynamic(this, &ThisClass::OnCombatEvent);
	OnDoorMotionFinished.AddUObject(this, &ThisClass::HandleDoorMotionFinished);
	OnDoorSafetyReopenStarted.AddUObject(this, &ThisClass::HandleDoorSafetyReopenStarted);
	EntranceCheckpoint->OnCheckpointCommitted.AddUObject(
		this, &ThisClass::OnEntranceCheckpointCommitted);
	TargetRoomVolume->OnRoomActorOverlapChanged.AddUObject(this, &ThisClass::OnRoomOverlapChanged);
	ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::ObservePlayerState));
	// 문보다 먼저 생긴 PlayerState와 나중에 접속한 PlayerState를 모두 구독한다.
	for (TActorIterator<AOutlierPlayerState> It(GetWorld()); It; ++It)
	{
		ObservePlayerState(*It);
	}
	// 리로드 중 새로 로드된 문은 이전 상태를 입장 조건으로 해석하지 않는다.
	if (!bAwaitingGameplayReady)
	{
		ReconcileRestoredProgress();
	}
	// RoomVolume과 PlayerState의 BeginPlay 순서와 무관하게 초기 입장을 한 번 더 판정한다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ThisClass::EvaluateEntry));
}

void ALevel1SuitUpgradeDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EntryRecheckTimer);
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
	}
	if (ArenaSubsystem.IsValid())
	{
		ArenaSubsystem->OnArenaGameplayReloadStarted.RemoveAll(this);
		ArenaSubsystem->OnArenaGameplayReady.RemoveAll(this);
	}
	OnDoorMotionFinished.RemoveAll(this);
	OnDoorSafetyReopenStarted.RemoveAll(this);
	if (IsValid(EntranceCheckpoint))
	{
		EntranceCheckpoint->OnCheckpointCommitted.RemoveAll(this);
	}
	if (CombatSubsystem.IsValid())
	{
		CombatSubsystem->OnRoomStartReadinessChanged.RemoveAll(this);
		CombatSubsystem->OnCombatEvent.RemoveDynamic(this, &ThisClass::OnCombatEvent);
	}
	if (IsValid(TargetRoomVolume))
	{
		TargetRoomVolume->OnRoomActorOverlapChanged.RemoveAll(this);
	}
	for (const TWeakObjectPtr<AOutlierPlayerState>& PlayerState : ObservedPlayerStates)
	{
		if (PlayerState.IsValid())
		{
			PlayerState->OnPlayerRoleChanged.RemoveAll(this);
			PlayerState->OnPlayerCharactersChanged.RemoveAll(this);
			PlayerState->OnStatAllocatorUICompleted.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ALevel1SuitUpgradeDoor::ObservePlayerState(AActor* Actor)
{
	AOutlierPlayerState* PlayerState = Cast<AOutlierPlayerState>(Actor);
	if (!PlayerState || ObservedPlayerStates.ContainsByPredicate(
		[PlayerState](const TWeakObjectPtr<AOutlierPlayerState>& Observed)
		{
			return Observed.Get() == PlayerState;
		}))
	{
		return;
	}
	ObservedPlayerStates.Add(PlayerState);
	PlayerState->OnPlayerRoleChanged.AddUObject(this, &ThisClass::OnPlayerStateChanged);
	PlayerState->OnPlayerCharactersChanged.AddUObject(this, &ThisClass::OnPlayerStateChanged);
	PlayerState->OnStatAllocatorUICompleted.AddUObject(this, &ThisClass::OnUICompleted);
	EvaluateEntry();
}

void ALevel1SuitUpgradeDoor::OnPlayerStateChanged(AOutlierPlayerState* PlayerState)
{
	(void)PlayerState;
	EvaluateEntry();
}

void ALevel1SuitUpgradeDoor::OnUICompleted(AOutlierPlayerState* PlayerState, uint32 CompletedGeneration)
{
	UE_LOG(LogTemp, Display,
		TEXT("[Level1Door] UI completed signal. Door=%s PlayerState=%s CompletedGeneration=%u CurrentGeneration=%u Sealed=%d CloseFinished=%d"),
		*GetNameSafe(this), *GetNameSafe(PlayerState), CompletedGeneration,
		GameplayGeneration, bEntrySealed, bCloseFinished);
	if (CompletedGeneration == GameplayGeneration)
	{
		EvaluateReopen();
	}
}

void ALevel1SuitUpgradeDoor::OnRoomOverlapChanged(AActor* Actor, bool bEntered)
{
	if (Cast<AShooterCharacter>(Actor) || Cast<APartnerCharacter>(Actor))
	{
		const TWeakObjectPtr<AActor> Player(Actor);
		if (bEntered)
		{
			OverlappingPlayers.Add(Player);
		}
		else
		{
			OverlappingPlayers.Remove(Player);
		}
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Room overlap. Door=%s Room=%s Actor=%s Entered=%d Generation=%u Ready=%d Sealed=%d"),
			*GetNameSafe(this), IsValid(TargetRoomVolume)
				? *TargetRoomVolume->GetRoomTag().ToString() : TEXT("None"),
			*GetNameSafe(Actor), bEntered, GameplayGeneration,
			!bAwaitingGameplayReady, bEntrySealed);
	}
	// 입장/퇴장 모두 현재 페어 위치를 다시 판정한다. 태그만 남은 이전 위치는 인정하지 않는다.
	if (AbortEntryIfPairOutside())
	{
		return;
	}
	EvaluateEntry();
}

void ALevel1SuitUpgradeDoor::OnRoomStartReadinessChanged(FGameplayTag ChangedRoomTag)
{
	if (IsValid(CombatRoomVolume) && ChangedRoomTag == CombatRoomVolume->GetRoomTag())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Room readiness changed. Door=%s Room=%s OpenFinished=%d CombatStarted=%d Generation=%u"),
			*GetNameSafe(this), *ChangedRoomTag.ToString(), bOpenFinished,
			bCombatStartSucceeded, GameplayGeneration);
		TryStartCombat();
	}
}

void ALevel1SuitUpgradeDoor::OnEntranceCheckpointCommitted(AOutlierCheckpoint* Checkpoint)
{
	if (HasAuthority() && Checkpoint == EntranceCheckpoint && !bAwaitingGameplayReady)
	{
		TryStartCombat();
	}
}

void ALevel1SuitUpgradeDoor::OnCombatEvent(
	FGameplayTag EventRoomTag, ERoomCombatEvent Event,
	int32 CombatPhaseIndex, int32 EventGeneration)
{
	(void)EventRoomTag;
	(void)CombatPhaseIndex;
	if (EventGeneration == static_cast<int32>(GameplayGeneration)
		&& Event != ERoomCombatEvent::SequenceStarted)
	{
		// 다른 Room이 끝난 뒤 서버의 단일 활성 전투 슬롯이 비면 대기 중인 문을 다시 확인한다.
		TryStartCombat();
	}
}

void ALevel1SuitUpgradeDoor::OnArenaReloadStarted(uint32 NewGeneration)
{
	GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
	OverlappingPlayers.Reset();
	// 이전 문/위젯 콜백이 다음 플레이 세대의 진행 플래그를 재사용하지 못하게 끊는다.
	GameplayGeneration = NewGeneration;
	bEntrySealed = false;
	bCloseFinished = false;
	bReopenRequested = false;
	bEntryCloseRejected = false;
	bOpenFinished = false;
	bCombatStartSucceeded = false;
	bCombatStartInProgress = false;
	bAwaitingGameplayReady = true;
	LastEntryStatus.Reset();
	if (IsValid(EntranceCheckpoint) && !EntranceCheckpoint->IsCheckpointCommitted())
	{
		EntranceCheckpoint->SetActivationConditionSatisfied(nullptr, false);
	}
}

void ALevel1SuitUpgradeDoor::OnArenaGameplayReady(uint32 ReadyGeneration)
{
	if (!HasAuthority() || !bAwaitingGameplayReady || ReadyGeneration != GameplayGeneration)
	{
		return;
	}
	bAwaitingGameplayReady = false;
	ReconcileRestoredProgress();
}

void ALevel1SuitUpgradeDoor::ReconcileRestoredProgress()
{
	if (!HasAuthority() || !IsValid(CombatRoomVolume) || !ArenaSubsystem.IsValid()
		|| GameplayGeneration != ArenaSubsystem->GetGameplayGeneration())
	{
		return;
	}
	UOutlierSaveSubSystem* Save = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	const FGameplayTag RoomTag = CombatRoomVolume->GetRoomTag();
	const bool bEncounterCleared = Save && Save->HasWorldProgress(
		EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName());
	const bool bRestoredOpen = Save && !DoorId.IsNone() && Save->HasWorldProgress(
		EOutlierWorldProgressType::OpenedDoor, DoorId);
	FOutlierCheckpointSnapshot RestoreSnapshot;
	const bool bSavedPastEntrance = Save && Save->GetRestoreSnapshot(RestoreSnapshot)
		&& !RestoreSnapshot.bInitialSnapshot && IsValid(EntranceCheckpoint)
		&& RestoreSnapshot.CheckpointId != EntranceCheckpoint->GetCheckpointId();

	// Suit 보유만으로 입장 연출을 건너뛰지 않는다. 저장된 문 열림 또는 전투 완료만
	// 연출이 이미 끝났다는 근거로 사용한다.
	if (bRestoredOpen || bEncounterCleared)
	{
		SnapDoorState(true);
		bEntrySealed = true;
		bCloseFinished = true;
		bReopenRequested = true;
		bOpenFinished = true;
		bCombatStartSucceeded = bEncounterCleared || bSavedPastEntrance;
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Progress restored. Door=%s Room=%s Generation=%u Open=%d EncounterCleared=%d"),
			*GetNameSafe(this), *RoomTag.ToString(), GameplayGeneration,
			bRestoredOpen, bEncounterCleared);
		if (!bCombatStartSucceeded && IsValid(EntranceCheckpoint))
		{
			EntranceCheckpoint->SetCombatRoomTag(RoomTag);
			if (!EntranceCheckpoint->IsCheckpointCommitted())
			{
				EntranceCheckpoint->SetActivationConditionSatisfied(nullptr, true);
			}
			TryStartCombat();
		}
		return;
	}

	// 문 열림 기록이 없는 복귀는 Suit 보유 여부와 무관하게 입장을 다시 판정한다.
	SnapDoorState(true);
	EvaluateEntry();
}

bool ALevel1SuitUpgradeDoor::FindPair(
	AOutlierPlayerState*& OutShooter, AOutlierPlayerState*& OutPartner) const
{
	OutShooter = nullptr;
	OutPartner = nullptr;
	for (TActorIterator<AOutlierPlayerState> It(GetWorld()); It; ++It)
	{
		AOutlierPlayerState* Candidate = *It;
		if (Candidate->GetPairId() == INDEX_NONE)
		{
			continue;
		}
		if (Candidate->GetPlayerRole() == EOutlierPlayerRole::Shooter
			&& IsValid(Candidate->GetShooterCharacter()))
		{
			OutShooter = Candidate;
		}
		else if (Candidate->GetPlayerRole() == EOutlierPlayerRole::Partner
			&& IsValid(Candidate->GetPartnerCharacter()))
		{
			OutPartner = Candidate;
		}
	}
	return OutShooter && OutPartner && OutShooter->GetPairId() == OutPartner->GetPairId();
}

bool ALevel1SuitUpgradeDoor::IsInsideRoom(const AActor* Character) const
{
	if (!IsValid(Character) || !IsValid(TargetRoomVolume)
		|| !TargetRoomVolume->ContainsWorldLocation(Character->GetActorLocation()))
	{
		return false;
	}
	const ACharacter* Pawn = Cast<ACharacter>(Character);
	const UCapsuleComponent* Capsule = Pawn ? Pawn->GetCapsuleComponent() : nullptr;
	const UBoxComponent* Box = Cast<UBoxComponent>(TargetRoomVolume->GetRootComponent());
	if (!Capsule || !Box)
	{
		return false;
	}
	// 닫힘 애니메이션 전에 캡슐 전체가 Box 안에 있어야 한다. 회전된 Box의 각 축에
	// 캡슐의 수직 선분과 구형 반경을 투영해 문턱이나 천장에 걸친 상태를 제외한다.
	const FVector CenterOffset = Capsule->GetComponentLocation() - Box->GetComponentLocation();
	const FVector Extent = Box->GetScaledBoxExtent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float SegmentHalfLength = FMath::Max(0.0f, Capsule->GetScaledCapsuleHalfHeight() - Radius);
	const FVector BoxAxes[] = { Box->GetForwardVector(), Box->GetRightVector(), Box->GetUpVector() };
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const float CapsuleReach = Radius
			+ FMath::Abs(FVector::DotProduct(BoxAxes[AxisIndex], Capsule->GetUpVector())) * SegmentHalfLength;
		if (FMath::Abs(FVector::DotProduct(CenterOffset, BoxAxes[AxisIndex])) + CapsuleReach
			> Extent[AxisIndex])
		{
			return false;
		}
	}
	const URoomTagComponent* Room = Cast<URoomTagComponent>(
		Character->GetComponentByClass(URoomTagComponent::StaticClass()));
	return Room && Room->GetCurrentRoomTag() == TargetRoomVolume->GetRoomTag();
}

void ALevel1SuitUpgradeDoor::EvaluateEntry()
{
	if (!HasAuthority() || bAwaitingGameplayReady || bEntrySealed || bEntryCloseRejected
		|| !IsValid(TargetRoomVolume))
	{
		return;
	}
	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	// RoomTag만 일치해서는 부족하다. 두 캡슐 모두 Volume 안에 들어온 뒤 닫힘 애니메이션을 시작한다.
	if (!FindPair(Shooter, Partner))
	{
		GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
		LogEntryStatus(TEXT("PairNotReady"), Shooter, Partner);
		return;
	}
	if (!IsInsideRoom(Shooter->GetShooterCharacter())
		|| !IsInsideRoom(Partner->GetPartnerCharacter()))
	{
		// 캡슐이 Box 경계에 닿으면 RoomTag가 먼저 바뀌지만 몸 전체는 아직 밖일 수 있다.
		// 실제 오버랩 중인 두 플레이어만 재검사해 방 밖에서 타이머가 계속 도는 일을 막는다.
		if (OverlappingPlayers.Contains(TWeakObjectPtr<AActor>(Shooter->GetShooterCharacter()))
			&& OverlappingPlayers.Contains(TWeakObjectPtr<AActor>(Partner->GetPartnerCharacter())))
		{
			if (!GetWorldTimerManager().IsTimerActive(EntryRecheckTimer))
			{
				GetWorldTimerManager().SetTimer(EntryRecheckTimer,
					this, &ThisClass::EvaluateEntry, 0.1f, true);
			}
		}
		else
		{
			GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
		}
		LogEntryStatus(TEXT("PlayerOutsideOrRoomTagMismatch"), Shooter, Partner);
		return;
	}
	GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
	if (!IsDoorOpen() || !HasMovementCurve())
	{
		LogEntryStatus(TEXT("DoorClosedOrCurveMissing"), Shooter, Partner);
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Cannot close door. Door=%s Room=%s Generation=%u Open=%d Curve=%d"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration,
			IsDoorOpen(), HasMovementCurve());
		return;
	}

	// 여기서는 닫기만 요청한다. 닫힘 완료 전에는 UI가 모두 끝나도 재개방하지 않는다.
	LogEntryStatus(TEXT("BothPlayersInside"), Shooter, Partner);
	if (!TrySetDoorOpen(false))
	{
		// 닫기 거절은 봉쇄 성공이 아니다. 반복 입장/상태 이벤트도 같은 요청을
		// 재시도하지 않게 대기한다. 새 요청을 만드는 진행 정책은 별도 Slice에서 연결한다.
		bEntryCloseRejected = true;
		LogEntryStatus(TEXT("DoorCloseRejected"), Shooter, Partner);
		return;
	}
	bEntrySealed = true;
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Entry sealed. Door=%s Room=%s Generation=%u"),
		*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
}

bool ALevel1SuitUpgradeDoor::AbortEntryIfPairOutside()
{
	if (!HasAuthority() || bAwaitingGameplayReady || !bEntrySealed || bReopenRequested
		|| !ArenaSubsystem.IsValid() || GameplayGeneration != ArenaSubsystem->GetGameplayGeneration())
	{
		return false;
	}
	// 퇴장 오버랩이 문 Tick보다 먼저 올 수 있다. 보호 대상이 경로에 있다면
	// 기존 즉시 Snap보다 안전 반전을 우선해 현재 위치에서 열고 새 요청을 기다린다.
	if (TrySafetyReopen())
	{
		return true;
	}

	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	if (FindPair(Shooter, Partner)
		&& IsInsideRoom(Shooter->GetShooterCharacter())
		&& IsInsideRoom(Partner->GetPartnerCharacter()))
	{
		return false;
	}

	// 닫는 중에 한 명이 빠졌다면 입장 확정을 되돌린다. Snap은 문 개방 완료/저장 이벤트를 만들지 않는다.
	UE_LOG(LogTemp, Warning,
		TEXT("[Level1Door] Entry cancelled. Door=%s Room=%s Generation=%u CloseFinished=%d ShooterInside=%d PartnerInside=%d"),
		*GetNameSafe(this), IsValid(TargetRoomVolume)
			? *TargetRoomVolume->GetRoomTag().ToString() : TEXT("None"),
		GameplayGeneration, bCloseFinished,
		Shooter && IsInsideRoom(Shooter->GetShooterCharacter()),
		Partner && IsInsideRoom(Partner->GetPartnerCharacter()));
	bEntrySealed = false;
	bCloseFinished = false;
	SnapDoorState(true);
	EvaluateEntry();
	return true;
}

void ALevel1SuitUpgradeDoor::LogEntryStatus(
	const TCHAR* Reason, const AOutlierPlayerState* Shooter, const AOutlierPlayerState* Partner)
{
	const auto DescribePlayer = [this](const AOutlierPlayerState* PlayerState, const AActor* Character)
	{
		const URoomTagComponent* Room = Character
			? Cast<URoomTagComponent>(Character->GetComponentByClass(URoomTagComponent::StaticClass()))
			: nullptr;
		return FString::Printf(TEXT("PS=%s Pair=%d Character=%s InVolume=%d CurrentRoom=%s"),
			*GetNameSafe(PlayerState), PlayerState ? PlayerState->GetPairId() : INDEX_NONE,
			*GetNameSafe(Character), IsValid(Character) && IsValid(TargetRoomVolume)
				&& TargetRoomVolume->ContainsWorldLocation(Character->GetActorLocation()),
			Room ? *Room->GetCurrentRoomTag().ToString() : TEXT("None"));
	};
	const FString Status = FString::Printf(TEXT("Reason=%s Shooter{%s} Partner{%s} Open=%d Curve=%d"),
		Reason, *DescribePlayer(Shooter, Shooter ? Shooter->GetShooterCharacter() : nullptr),
		*DescribePlayer(Partner, Partner ? Partner->GetPartnerCharacter() : nullptr),
		IsDoorOpen(), HasMovementCurve());
	if (Status == LastEntryStatus)
	{
		return;
	}
	LastEntryStatus = Status;
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Entry check. Door=%s Room=%s Generation=%u %s"),
		*GetNameSafe(this), IsValid(TargetRoomVolume)
			? *TargetRoomVolume->GetRoomTag().ToString() : TEXT("None"), GameplayGeneration, *Status);
}

void ALevel1SuitUpgradeDoor::EvaluateReopen()
{
	// UI 완료 이벤트가 먼저 오면 대기하고, 닫힘 Timeline 완료 콜백에서 같은 조건을 다시 본다.
	if (!HasAuthority() || !bEntrySealed || !bCloseFinished || bReopenRequested
		|| !IsValid(TargetRoomVolume))
	{
		return;
	}
	if (AbortEntryIfPairOutside())
	{
		return;
	}
	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	if (!FindPair(Shooter, Partner)
		|| !Shooter->IsStatAllocatorUICompletedForGeneration(GameplayGeneration)
		|| !Partner->IsStatAllocatorUICompletedForGeneration(GameplayGeneration))
	{
		return;
	}
	bReopenRequested = true;
	SetDoorOpen(true);
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Reopening. Door=%s Room=%s Generation=%u"),
		*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
}

bool ALevel1SuitUpgradeDoor::TryStartCombat()
{
	if (!HasAuthority() || bAwaitingGameplayReady || !bOpenFinished
		|| bCombatStartSucceeded || bCombatStartInProgress
		|| !IsValid(CombatRoomVolume) || !CombatSubsystem.IsValid()
		|| !IsValid(EntranceCheckpoint) || !EntranceCheckpoint->IsCheckpointCommitted())
	{
		return false;
	}
	if (!ArenaSubsystem.IsValid()
		|| GameplayGeneration != ArenaSubsystem->GetGameplayGeneration())
	{
		return false;
	}

	const FGameplayTag RoomTag = CombatRoomVolume->GetRoomTag();
	// 문은 이미 열렸어도 WP의 Room/SpawnPoint 등록이 늦으면 그대로 대기한다.
	// 등록 알림이나 다른 Room 전투 종료 알림에서 이 함수를 다시 호출한다.
	if (!CombatSubsystem->HasReadyExternalTriggerRoster(CombatRoomVolume, RoomTag))
	{
		return false;
	}
	FRoomCombatTriggerContext Context;
	const uint32 StartGeneration = GameplayGeneration;
	bCombatStartInProgress = true;
	const bool bStarted = CombatSubsystem->CreateTriggerContext(
		this, RoomTag, FGameplayTag(), Context)
		&& CombatSubsystem->StartTriggeredSequence(this, Context);
	bCombatStartInProgress = false;
	if (GameplayGeneration != StartGeneration
		|| ArenaSubsystem->GetGameplayGeneration() != StartGeneration)
	{
		return false;
	}
	if (!bStarted)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Level1Door] Combat start failed. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *RoomTag.ToString(), GameplayGeneration);
		return false;
	}
	bCombatStartSucceeded = true;
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Combat started. Door=%s Room=%s Generation=%u"),
		*GetNameSafe(this), *RoomTag.ToString(), GameplayGeneration);
	return true;
}

void ALevel1SuitUpgradeDoor::HandleDoorSafetyReopenStarted(AInteractableDoor* Door)
{
	if (!HasAuthority() || Door != this)
	{
		return;
	}
	// 닫힘 중단 -> 봉쇄/정상 개방 완료 취소 -> 재평가 차단 순서로 정리한다.
	// 안전 개방 후의 새 입장 요청 조건은 후속 Slice에서 연결한다.
	GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
	bEntrySealed = false;
	bCloseFinished = false;
	bReopenRequested = false;
	bOpenFinished = false;
	bEntryCloseRejected = true;
	if (IsValid(EntranceCheckpoint) && !EntranceCheckpoint->IsCheckpointCommitted())
	{
		EntranceCheckpoint->SetActivationConditionSatisfied(nullptr, false);
	}
	LogEntryStatus(TEXT("DoorSafetyReopen"), nullptr, nullptr);
}

void ALevel1SuitUpgradeDoor::HandleDoorMotionFinished(AInteractableDoor* Door, bool bOpen)
{
	if (!HasAuthority() || Door != this || !ArenaSubsystem.IsValid()
		|| !IsValid(TargetRoomVolume)
		|| GameplayGeneration != ArenaSubsystem->GetGameplayGeneration())
	{
		return;
	}
	if (!bOpen && bEntrySealed && !bCloseFinished)
	{
		if (AbortEntryIfPairOutside())
		{
			return;
		}
		// 1단계: 닫힌 위치 도착. 지금까지 누적된 양쪽 UI 완료 여부를 확인한다.
		bCloseFinished = true;
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Door closed. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
		EvaluateReopen();
	}
	else if (bOpen && bReopenRequested && !bOpenFinished)
	{
		// 2단계: 열림 완료 후 입구 저장을 허용한다. 전투는 체크포인트 확정 통지까지 대기한다.
		bOpenFinished = true;
		UE_LOG(LogTemp, Display, TEXT("[Level1Door] Door opened. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
		OnLevel1DoorOpened.Broadcast(this, GameplayGeneration);
		if (IsValid(EntranceCheckpoint) && IsValid(CombatRoomVolume))
		{
			EntranceCheckpoint->SetCombatRoomTag(CombatRoomVolume->GetRoomTag());
			EntranceCheckpoint->SetActivationConditionSatisfied(nullptr, true);
		}
	}
}
