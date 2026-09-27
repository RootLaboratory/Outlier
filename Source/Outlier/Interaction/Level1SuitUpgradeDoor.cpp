#include "Interaction/Level1SuitUpgradeDoor.h"

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
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"

// Level 1 진행의 서버 측 순서:
// 두 플레이어가 입장 Room 안에 모임 -> 이 문 닫기 -> 닫힘 Timeline 완료
// -> 양쪽 UI의 실제 종료 확인 -> 이 문 열기 -> 열림 Timeline 완료
// -> 별도 전투 Room과 첫 Wave의 SpawnPoint가 준비되면 ExternalTrigger 전투 시작.
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
	bOpenFinished = false;
	bCombatStartSucceeded = false;
	bCombatStartInProgress = false;
	bAwaitingGameplayReady = true;
	LastEntryStatus.Reset();
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
	const UOutlierSaveSubSystem* Save = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	const FGameplayTag RoomTag = CombatRoomVolume->GetRoomTag();
	const bool bEncounterCleared = Save && Save->HasWorldProgress(
		EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName());
	const bool bRestoredOpen = Save && !DoorId.IsNone() && Save->HasWorldProgress(
		EOutlierWorldProgressType::OpenedDoor, DoorId);

	// 완료 기록과 문 열림 기록은 별도다. 복원은 Timeline 완료 이벤트를 만들지 않고
	// 전투가 남은 열린 문만 준비된 Room 명단을 기다려 한 번 재개한다.
	if (bRestoredOpen || bEncounterCleared)
	{
		SnapDoorState(true);
		bEntrySealed = true;
		bCloseFinished = true;
		bReopenRequested = true;
		bOpenFinished = true;
		bCombatStartSucceeded = bEncounterCleared;
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Progress restored. Door=%s Room=%s Generation=%u Open=%d EncounterCleared=%d"),
			*GetNameSafe(this), *RoomTag.ToString(), GameplayGeneration,
			bRestoredOpen, bEncounterCleared);
		if (!bEncounterCleared)
		{
			TryStartCombat();
		}
		return;
	}

	// 초기 스냅샷/프리셋 복귀는 이 문의 초기 열림 상태에서 다시 입장 판정한다.
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
	const URoomTagComponent* Room = Cast<URoomTagComponent>(
		Character->GetComponentByClass(URoomTagComponent::StaticClass()));
	return Room && Room->GetCurrentRoomTag() == TargetRoomVolume->GetRoomTag();
}

void ALevel1SuitUpgradeDoor::EvaluateEntry()
{
	if (!HasAuthority() || bAwaitingGameplayReady || bEntrySealed || !IsValid(TargetRoomVolume))
	{
		return;
	}
	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	// RoomTag만 일치해서는 부족하다. 현재 캐릭터 둘 다 같은 Volume 안에 있어야 닫는다.
	if (!FindPair(Shooter, Partner))
	{
		GetWorldTimerManager().ClearTimer(EntryRecheckTimer);
		LogEntryStatus(TEXT("PairNotReady"), Shooter, Partner);
		return;
	}
	if (!IsInsideRoom(Shooter->GetShooterCharacter())
		|| !IsInsideRoom(Partner->GetPartnerCharacter()))
	{
		// 캡슐이 Box 경계에 닿으면 RoomTag가 먼저 바뀌지만 Actor 원점은 아직 밖일 수 있다.
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
	bEntrySealed = true;
	SetDoorOpen(false);
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Entry sealed. Door=%s Room=%s Generation=%u"),
		*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
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
		|| !IsValid(CombatRoomVolume) || !CombatSubsystem.IsValid())
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
		// 1단계: 닫힌 위치 도착. 지금까지 누적된 양쪽 UI 완료 여부를 확인한다.
		bCloseFinished = true;
		UE_LOG(LogTemp, Display,
			TEXT("[Level1Door] Door closed. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
		EvaluateReopen();
	}
	else if (bOpen && bReopenRequested && !bOpenFinished)
	{
		// 2단계: 열린 위치 도착. 요청 시점이 아닌 이 시점만 전투 시작 신호가 된다.
		bOpenFinished = true;
		UE_LOG(LogTemp, Display, TEXT("[Level1Door] Door opened. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
		OnLevel1DoorOpened.Broadcast(this, GameplayGeneration);
		if (!TryStartCombat() && IsValid(CombatRoomVolume))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Level1Door] Combat start waiting. Door=%s Room=%s Generation=%u RegisteredSpawnPoints=%d"),
				*GetNameSafe(this), *CombatRoomVolume->GetRoomTag().ToString(), GameplayGeneration,
				CombatSubsystem.IsValid()
					? CombatSubsystem->GetRegisteredSpawnPointCount(CombatRoomVolume->GetRoomTag()) : 0);
		}
	}
}
