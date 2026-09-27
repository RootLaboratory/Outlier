#include "Interaction/Level1SuitUpgradeDoor.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/InteractableDoor.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"

// Level 1 진행의 서버 측 순서:
// 두 플레이어가 Room 안에 모임 -> 이 문 닫기 -> 닫힘 Timeline 완료
// -> 양쪽 UI의 실제 종료 확인 -> 이 문 열기 -> 열림 Timeline 완료 통지.
// 전투 시작은 이 통지를 받는 다음 Slice가 담당한다.
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

	if (!IsValid(TargetRoomVolume) || !TargetRoomVolume->GetRoomTag().IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Invalid room setup. Door=%s Room=%s"),
			*GetNameSafe(this), IsValid(TargetRoomVolume)
				? *TargetRoomVolume->GetRoomTag().ToString() : TEXT("None"));
		return;
	}

	UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Arena)
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Arena missing. Door=%s"), *GetNameSafe(this));
		return;
	}

	GameplayGeneration = Arena->GetGameplayGeneration();
	// RoomVolume은 입장, PlayerState는 UI 완료, 부모 Door는 연출 완료를 각각 알린다.
	// 이 자식은 순서만 조합하고 메시/Timeline은 부모 구현을 그대로 사용한다.
	Arena->OnArenaGameplayReloadStarted.AddUObject(this, &ThisClass::OnArenaReloadStarted);
	OnDoorMotionFinished.AddUObject(this, &ThisClass::HandleDoorMotionFinished);
	TargetRoomVolume->OnRoomActorOverlapChanged.AddUObject(this, &ThisClass::OnRoomOverlapChanged);
	ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::ObservePlayerState));
	// 문보다 먼저 생긴 PlayerState와 나중에 접속한 PlayerState를 모두 구독한다.
	for (TActorIterator<AOutlierPlayerState> It(GetWorld()); It; ++It)
	{
		ObservePlayerState(*It);
	}
	EvaluateEntry();
	// RoomVolume과 PlayerState의 BeginPlay 순서와 무관하게 초기 입장을 한 번 더 판정한다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ThisClass::EvaluateEntry));
}

void ALevel1SuitUpgradeDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
		if (UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>())
		{
			Arena->OnArenaGameplayReloadStarted.RemoveAll(this);
		}
	}
	OnDoorMotionFinished.RemoveAll(this);
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
	(void)PlayerState;
	if (CompletedGeneration == GameplayGeneration)
	{
		EvaluateReopen();
	}
}

void ALevel1SuitUpgradeDoor::OnRoomOverlapChanged(AActor* Actor, bool bEntered)
{
	(void)Actor;
	(void)bEntered;
	// 입장/퇴장 모두 현재 페어 위치를 다시 판정한다. 태그만 남은 이전 위치는 인정하지 않는다.
	EvaluateEntry();
}

void ALevel1SuitUpgradeDoor::OnArenaReloadStarted(uint32 NewGeneration)
{
	// 이전 문/위젯 콜백이 다음 플레이 세대의 진행 플래그를 재사용하지 못하게 끊는다.
	GameplayGeneration = NewGeneration;
	bEntrySealed = false;
	bCloseFinished = false;
	bReopenRequested = false;
	bOpenFinished = false;
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
	if (!HasAuthority() || bEntrySealed || !IsValid(TargetRoomVolume))
	{
		return;
	}
	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	// RoomTag만 일치해서는 부족하다. 현재 캐릭터 둘 다 같은 Volume 안에 있어야 닫는다.
	if (!FindPair(Shooter, Partner)
		|| !IsInsideRoom(Shooter->GetShooterCharacter())
		|| !IsInsideRoom(Partner->GetPartnerCharacter()))
	{
		return;
	}
	if (!IsDoorOpen() || !HasMovementCurve())
	{
		UE_LOG(LogTemp, Error, TEXT("[Level1Door] Cannot close door. Door=%s Room=%s Generation=%u Open=%d Curve=%d"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration,
			IsDoorOpen(), HasMovementCurve());
		return;
	}

	// 여기서는 닫기만 요청한다. 닫힘 완료 전에는 UI가 모두 끝나도 재개방하지 않는다.
	bEntrySealed = true;
	SetDoorOpen(false);
	UE_LOG(LogTemp, Display, TEXT("[Level1Door] Entry sealed. Door=%s Room=%s Generation=%u"),
		*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
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

void ALevel1SuitUpgradeDoor::HandleDoorMotionFinished(AInteractableDoor* Door, bool bOpen)
{
	const UOutlierArenaSubsystem* Arena = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	if (!HasAuthority() || Door != this || !Arena || !IsValid(TargetRoomVolume)
		|| GameplayGeneration != Arena->GetGameplayGeneration())
	{
		return;
	}
	if (!bOpen && bEntrySealed && !bCloseFinished)
	{
		// 1단계: 닫힌 위치 도착. 지금까지 누적된 양쪽 UI 완료 여부를 확인한다.
		bCloseFinished = true;
		EvaluateReopen();
	}
	else if (bOpen && bReopenRequested && !bOpenFinished)
	{
		// 2단계: 열린 위치 도착. 요청 시점이 아닌 이 시점만 전투 시작 신호가 된다.
		bOpenFinished = true;
		UE_LOG(LogTemp, Display, TEXT("[Level1Door] Door opened. Door=%s Room=%s Generation=%u"),
			*GetNameSafe(this), *TargetRoomVolume->GetRoomTag().ToString(), GameplayGeneration);
		OnLevel1DoorOpened.Broadcast(this, GameplayGeneration);
	}
}
