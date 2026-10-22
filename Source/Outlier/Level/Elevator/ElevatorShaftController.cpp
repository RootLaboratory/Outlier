#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Interaction/InteractableDoor.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "OutlierPlayerState.h"
#include "OutlierGameMode.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Room/RoomVolume.h"
#include "Room/RoomTagComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElevatorShaft, Log, All);

AElevatorShaftController::AElevatorShaftController()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ShaftOrigin")));
	EntranceBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("EntranceBlocker"));
	EntranceBlocker->SetupAttachment(RootComponent);
	EntranceBlocker->SetBoxExtent(FVector(30, 150, 150));
	EntranceBlocker->SetCollisionProfileName(TEXT("BlockAll"));
	EntranceBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EntranceBlocker->SetGenerateOverlapEvents(false);
	EntranceBlocker->SetCanEverAffectNavigation(false);
}

void AElevatorShaftController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ResolveEntranceActors();
}

AElevatorShaftSection* AElevatorShaftController::FindEntranceSection() const
{
	AElevatorShaftSection* FoundSection = nullptr;
	TInlineComponentArray<UChildActorComponent*> ChildComponents;
	GetComponents(ChildComponents, false);
	for (const UChildActorComponent* Component : ChildComponents)
	{
		if (AElevatorShaftSection* Section = Cast<AElevatorShaftSection>(Component->GetChildActor()))
		{
			// 여러 층 중 하나를 임의로 고르면 입구와 반복 층이 뒤바뀔 수 있다.
			if (FoundSection)
			{
				return nullptr;
			}
			FoundSection = Section;
		}
	}
	return FoundSection;
}

void AElevatorShaftController::ResolveEntranceActors()
{
	EntranceSection = FindEntranceSection();
	// 클라이언트는 BeginPlay 전에 받은 서버 문 참조를 빈 Child Actor 캐시로 덮지 않는다.
	if (HasAuthority())
	{
		EntranceDoor = nullptr;
	}
	if (EntranceSection)
	{
		// 중첩 Child Actor가 생성된 뒤 캐시를 갱신한다. 자식 BeginPlay 순서에 의존하지 않는다.
		EntranceSection->RefreshChildActors();
		if (AInteractableDoor* Door = EntranceSection->GetEntranceDoor())
		{
			EntranceDoor = Door;
		}
	}
}

void AElevatorShaftController::BeginPlay()
{
	Super::BeginPlay();
	// Section은 각 머신의 Child Actor를 사용하고, 실제 문 참조는 서버 복제로도 보완한다.
	ResolveEntranceActors();
	Clock = GetWorld()->GetGameState();
	Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	bInitialized = true;
	if (!HasAuthority())
	{
		OnRep_Motion();
		return;
	}
	Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	GameMode = GetWorld()->GetAuthGameMode<AOutlierGameMode>();
	Motion.EntranceTransform = EntranceSection ? EntranceSection->GetActorTransform() : GetActorTransform();
	Motion.Generation = Arena.IsValid() ? Arena->GetGameplayGeneration() : 0;
	Motion.ExecutionId = FGuid::NewGuid();
	Motion.Speed = RiseSpeed;
	FString Error;
	bConfigurationValid = ValidateConfiguration(Error);
	if (!bConfigurationValid)
	{
		UE_LOG(LogElevatorShaft, Error, TEXT("%s: %s"), *GetName(), *Error);
		return;
	}
	EntryRoom->OnRoomActorOverlapChanged.AddUObject(this, &ThisClass::OnOverlap);
	EntranceDoor->OnDoorMotionFinished.AddUObject(this, &ThisClass::OnDoorFinished);
	EntranceDoor->OnDoorSafetyReopenStarted.AddUObject(this, &ThisClass::OnDoorReopened);
	Combat->OnCombatEvent.AddDynamic(this, &ThisClass::OnCombatEvent);
	Combat->OnRoomStartReadinessChanged.AddUObject(this, &ThisClass::OnReadinessChanged);
	Combat->OnWaveFirstSpawned.AddUObject(this, &ThisClass::OnFirstSpawn);
	if (Arena.IsValid())
	{
		Arena->OnArenaGameplayReloadStarted.AddUObject(this, &ThisClass::OnReload);
	}
	if (GameMode.IsValid())
	{
		GameMode->OnPairGameplayReady.AddUObject(this, &ThisClass::OnPairReady);
	}
	InitializeVisualPool();
	// 레벨 Actor의 BeginPlay 순서와 무관하게 구독을 끝낸 다음 현재 준비 상태를 확인한다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ScheduleEntryEvaluation);
}

void AElevatorShaftController::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	ReleaseTransition();
	if (EntryRoom)
	{
		EntryRoom->OnRoomActorOverlapChanged.RemoveAll(this);
	}
	if (EntranceDoor)
	{
		EntranceDoor->OnDoorMotionFinished.RemoveAll(this);
		EntranceDoor->OnDoorSafetyReopenStarted.RemoveAll(this);
	}
	if (Combat.IsValid())
	{
		Combat->OnCombatEvent.RemoveDynamic(this, &ThisClass::OnCombatEvent);
		Combat->OnRoomStartReadinessChanged.RemoveAll(this);
		Combat->OnWaveFirstSpawned.RemoveAll(this);
	}
	if (Arena.IsValid())
	{
		Arena->OnArenaGameplayReloadStarted.RemoveAll(this);
	}
	if (GameMode.IsValid())
	{
		GameMode->OnPairGameplayReady.RemoveAll(this);
	}
	for (const auto& Player : ObservedPlayers)
	{
		if (Player.IsValid())
		{
			Player->OnPlayerCharactersChanged.RemoveAll(this);
			Player->OnPlayerRoleChanged.RemoveAll(this);
		}
	}
	for (AElevatorShaftSection* Section : LoopSections)
	{
		if (IsValid(Section))
		{
			Section->Destroy();
		}
	}
	LoopSections.Reset();
	Super::EndPlay(Reason);
}

bool AElevatorShaftController::ValidateConfiguration(FString& Error) const
{
	// 저장된 레벨을 검증할 때는 Transient 캐시가 아직 비어 있을 수 있으므로 계층을 직접 확인한다.
	const AElevatorShaftSection* Section = FindEntranceSection();
	if (!Section)
	{
		Error = TEXT("Controller must contain exactly one ElevatorShaftSection Child Actor. Add the Entrance Section BP as a Child Actor Component.");
		return false;
	}
	TArray<AActor*> SectionActors;
	Section->GetAllChildActors(SectionActors, true);
	int32 DoorCount = 0;
	for (const AActor* SectionActor : SectionActors)
	{
		if (Cast<AInteractableDoor>(SectionActor))
		{
			++DoorCount;
		}
	}
	if (DoorCount != 1)
	{
		Error = TEXT("Entrance Section must contain exactly one InteractableDoor Child Actor, including nested wall Child Actors.");
		return false;
	}
	if (!EntryRoom || !CombatRoomTag.IsValid()
		|| !LoopSectionClass || !ShooterExitTarget || !PartnerExitTarget || !ExitRoom)
	{
		Error = TEXT("Entry/Combat Room, Loop class, Exit Room/targets are required.");
		return false;
	}
	const AElevatorShaftSection* Loop = LoopSectionClass.GetDefaultObject();
	if (!FMath::IsFinite(RiseSpeed) || RiseSpeed <= 0 || LoopSectionCount < 3
		|| !FMath::IsFinite(Section->GetSectionHeight()) || Section->GetSectionHeight() <= 0
		|| !Section->GetActorScale3D().Equals(FVector::OneVector)
		|| !Section->GetActorUpVector().Equals(FVector::UpVector)
		|| !FMath::IsFinite(FadeOutDuration) || FadeOutDuration < 0
		|| !FMath::IsFinite(FadeInDuration) || FadeInDuration < 0
		|| !FMath::IsFinite(TransitionTimeout) || TransitionTimeout <= 0
		|| !FMath::IsNearlyEqual(Section->GetSectionHeight(), Loop->GetSectionHeight()))
	{
		Error = TEXT("Invalid speed, pool count, section transform, section heights or transition timing.");
		return false;
	}
	if (ShooterExitTarget == PartnerExitTarget || ExitRoom == EntryRoom
		|| ShooterExitTarget->GetWorld() != GetWorld() || PartnerExitTarget->GetWorld() != GetWorld())
	{
		Error = TEXT("Exit requires distinct role targets in a separate room of the same world.");
		return false;
	}
	return true;
}

bool AElevatorShaftController::CachePair()
{
	AOutlierPlayerState* Shooter = nullptr;
	AOutlierPlayerState* Partner = nullptr;
	if (!Clock.IsValid())
	{
		Clock = GetWorld()->GetGameState();
	}
	if (!Clock.IsValid())
	{
		return false;
	}
	for (APlayerState* State : Clock->PlayerArray)
	{
		AOutlierPlayerState* Player = Cast<AOutlierPlayerState>(State);
		if (!Player || Player->GetPairId() == INDEX_NONE)
		{
			continue;
		}
		if (!ObservedPlayers.Contains(Player))
		{
			ObservedPlayers.Add(Player);
			Player->OnPlayerCharactersChanged.AddUObject(this, &ThisClass::OnPlayerChanged);
			Player->OnPlayerRoleChanged.AddUObject(this, &ThisClass::OnPlayerChanged);
		}
		if (Player->GetPlayerRole() == EOutlierPlayerRole::Shooter)
		{
			Shooter = Player;
		}
		if (Player->GetPlayerRole() == EOutlierPlayerRole::Partner)
		{
			Partner = Player;
		}
	}
	if (!Shooter || !Partner || Shooter->GetPairId() != Partner->GetPairId())
	{
		return false;
	}
	ShooterController = Cast<AFirstPersonPlayerController>(Shooter->GetOwner());
	PartnerController = Cast<AFirstPersonPlayerController>(Partner->GetOwner());
	return ShooterController.IsValid() && PartnerController.IsValid()
		&& ShooterController->GetPawn() && PartnerController->GetPawn();
}

bool AElevatorShaftController::IsInside(const APawn* Pawn) const
{
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	const UBoxComponent* Box = EntryRoom ? Cast<UBoxComponent>(EntryRoom->GetRootComponent()) : nullptr;
	if (!Capsule || !Box)
	{
		return false;
	}
	const FVector Delta = Capsule->GetComponentLocation() - Box->GetComponentLocation();
	const FVector Axes[] = {Box->GetForwardVector(), Box->GetRightVector(), Box->GetUpVector()};
	const FVector Extent = Box->GetScaledBoxExtent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Segment = FMath::Max(0.f, Capsule->GetScaledCapsuleHalfHeight() - Radius);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Reach = Radius + FMath::Abs(FVector::DotProduct(Axes[Index], Capsule->GetUpVector())) * Segment;
		if (FMath::Abs(FVector::DotProduct(Delta, Axes[Index])) + Reach > Extent[Index])
		{
			return false;
		}
	}
	return true;
}

bool AElevatorShaftController::IsPairInside() const
{
	return ShooterController.IsValid() && PartnerController.IsValid()
		&& IsInside(ShooterController->GetPawn()) && IsInside(PartnerController->GetPawn());
}

void AElevatorShaftController::EvaluateEntry()
{
	if (!HasAuthority() || !bConfigurationValid || !GameMode.IsValid()
		|| GameMode->IsGameOverSelectionActive() || !GameMode->IsPairGameplayReady() || !CachePair())
	{
		return;
	}
	if (Motion.State != EElevatorShaftState::WaitingForPair)
	{
		return;
	}
	if (GameMode->IsRestoringClearedEncounter(CombatRoomTag))
	{
		SetEntranceBlocked(true);
		BeginExit();
		return;
	}
	if (bCloseRejected || !IsPairInside() || !Combat.IsValid() || !Combat->IsRoomRegistered(CombatRoomTag))
	{
		return;
	}
	if (!Combat->IsSingleExternalTriggerEncounter(CombatRoomTag))
	{
		UE_LOG(LogElevatorShaft, Error, TEXT("%s requires one ExternalTrigger phase with spawn waves."), *GetName());
		return;
	}
	if (!Combat->CreateTriggerContext(this, CombatRoomTag, FGameplayTag(), CombatContext))
	{
		return;
	}
	SetState(EElevatorShaftState::ClosingEntrance);
	if (!EntranceDoor->TrySetDoorOpen(false))
	{
		bCloseRejected = true;
		SetState(EElevatorShaftState::WaitingForPair);
		return;
	}
	// 이미 닫힌 문 또는 Curve 없는 즉시 닫힘도 실제 정지 상태를 확인하고 진행한다.
	if (!EntranceDoor->IsDoorOpen() && !EntranceDoor->IsDoorMoving())
	{
		SealEntrance();
	}
}

void AElevatorShaftController::OnOverlap(AActor* Actor, bool bEntered)
{
	// 단순 재확인 타이머는 거부된 닫힘을 다시 강제하지 않는다. 새 진입 변화만 시도를 허용한다.
	if (Actor && ((ShooterController.IsValid() && Actor == ShooterController->GetPawn())
		|| (PartnerController.IsValid() && Actor == PartnerController->GetPawn())))
	{
		bCloseRejected = false;
	}
	if (Motion.State == EElevatorShaftState::ClosingEntrance && !IsPairInside())
	{
		SetState(EElevatorShaftState::WaitingForPair);
		EntranceDoor->TrySetDoorOpen(true);
		return;
	}
	ScheduleEntryEvaluation();
}

void AElevatorShaftController::ScheduleEntryEvaluation()
{
	EvaluateEntry();
	if (!bCloseRejected && Motion.State == EElevatorShaftState::WaitingForPair && EntryRoom
		&& ((ShooterController.IsValid() && EntryRoom->IsOverlappingActor(ShooterController->GetPawn()))
			|| (PartnerController.IsValid() && EntryRoom->IsOverlappingActor(PartnerController->GetPawn()))))
	{
		// BeginOverlap은 캡슐 일부가 들어온 시점이다. 완전히 들어오는 순간까지 진입 구역에서만 재확인한다.
		if (!GetWorldTimerManager().IsTimerActive(EntryCheckTimer))
		{
			GetWorldTimerManager().SetTimer(EntryCheckTimer, this, &ThisClass::RecheckEntry, 0.15f, true);
		}
	}
}

void AElevatorShaftController::RecheckEntry()
{
	EvaluateEntry();
	if (bCloseRejected || Motion.State != EElevatorShaftState::WaitingForPair || !EntryRoom
		|| !((ShooterController.IsValid() && EntryRoom->IsOverlappingActor(ShooterController->GetPawn()))
			|| (PartnerController.IsValid() && EntryRoom->IsOverlappingActor(PartnerController->GetPawn()))))
	{
		GetWorldTimerManager().ClearTimer(EntryCheckTimer);
	}
}

void AElevatorShaftController::OnPlayerChanged(AOutlierPlayerState* Player)
{
	bCloseRejected = false;
	ScheduleEntryEvaluation();
}

void AElevatorShaftController::OnReadinessChanged(FGameplayTag Room)
{
	if (Room != CombatRoomTag)
	{
		return;
	}
	bCloseRejected = false;
	if (Motion.State == EElevatorShaftState::AwaitingSpawn && !bCombatRequested)
	{
		StartCombat();
	}
	else
	{
		ScheduleEntryEvaluation();
	}
}

void AElevatorShaftController::OnPairReady(uint32 Generation)
{
	if (Generation == Motion.Generation)
	{
		bCloseRejected = false;
		ScheduleEntryEvaluation();
	}
}

void AElevatorShaftController::OnDoorFinished(AInteractableDoor* Door, bool bOpen)
{
	if (Door == EntranceDoor && !bOpen && Motion.State == EElevatorShaftState::ClosingEntrance)
	{
		SealEntrance();
	}
}

void AElevatorShaftController::OnDoorReopened(AInteractableDoor* Door)
{
	if (Door == EntranceDoor && Motion.State == EElevatorShaftState::ClosingEntrance)
	{
		// 안전 재개방 뒤 자동으로 다시 닫지 않는다. 다음 실제 진입/준비 알림을 기다린다.
		Motion.ExecutionId = FGuid::NewGuid();
		bCloseRejected = true;
		SetState(EElevatorShaftState::WaitingForPair);
	}
}

void AElevatorShaftController::SealEntrance()
{
	if (!IsPairInside() || EntranceDoor->IsDoorOpen() || EntranceDoor->IsDoorMoving())
	{
		SetState(EElevatorShaftState::WaitingForPair);
		EntranceDoor->TrySetDoorOpen(true);
		return;
	}
	// 고정 장벽을 먼저 켠 뒤 문 충돌을 해제한다. 움직이는 문이 캐릭터를 밀지 않게 한다.
	SetEntranceBlocked(true);
	if (!EntranceDoor->SetPresentationOnly(true))
	{
		SetEntranceBlocked(false);
		SetState(EElevatorShaftState::WaitingForPair);
		return;
	}
	if (bPlayIntro)
	{
		SetState(EElevatorShaftState::Intro);
		PlayIntroOnServer(Motion.ExecutionId);
	}
	else
	{
		StartCombat();
	}
}

void AElevatorShaftController::FinishIntro(FGuid ExecutionId)
{
	if (HasAuthority() && ExecutionId == Motion.ExecutionId && Motion.State == EElevatorShaftState::Intro
		&& (!Arena.IsValid() || Arena->GetGameplayGeneration() == Motion.Generation))
	{
		StartCombat();
	}
}

void AElevatorShaftController::StartCombat()
{
	if (!Combat.IsValid() || bStartingCombat || bCombatRequested)
	{
		return;
	}
	SetState(EElevatorShaftState::AwaitingSpawn);
	bStartingCombat = true;
	const FGuid StartingExecution = Motion.ExecutionId;
	const FRoomCombatTriggerContext StartingContext = CombatContext;
	const bool bStarted = Combat->StartTriggeredSequence(this, CombatContext);
	bStartingCombat = false;
	// SequenceStarted 수신자가 리로드/취소할 수 있다. 돌아온 이전 호출이 새 실행을 덮지 않게 한다.
	if (Motion.ExecutionId != StartingExecution || Motion.Generation != static_cast<uint32>(StartingContext.GameplayGeneration)
		|| (Motion.State != EElevatorShaftState::AwaitingSpawn && Motion.State != EElevatorShaftState::RisingCombat))
	{
		return;
	}
	bCombatRequested = bStarted;
	if (!bStarted)
	{
		UE_LOG(LogElevatorShaft, Warning, TEXT("%s: combat start pending; waiting for Room readiness."), *GetName());
		return;
	}
	// 즉시 스폰이 완료되어 알림이 재진입한 경우에도 상승을 중복 시작하지 않는다.
	if (Motion.State == EElevatorShaftState::AwaitingSpawn && Combat->HasCurrentWaveSpawned(CombatContext))
	{
		OnFirstSpawn(CombatRoomTag, CombatContext.CombatPhaseIndex, 0, CombatContext.GameplayGeneration, CombatContext.RoomRegistrationId);
	}
}

void AElevatorShaftController::OnFirstSpawn(FGameplayTag Room, int32 Phase, int32 Wave, int32 Generation, FGuid RegistrationId)
{
	if (!HasAuthority() || Motion.State != EElevatorShaftState::AwaitingSpawn || Room != CombatRoomTag
		|| Phase != CombatContext.CombatPhaseIndex || Generation != CombatContext.GameplayGeneration
		|| Generation != static_cast<int32>(Motion.Generation) || RegistrationId != CombatContext.RoomRegistrationId)
	{
		return;
	}
	Motion.StartServerTime = ServerTime();
	Motion.StoppedDistance = 0;
	SetState(EElevatorShaftState::RisingCombat);
}

void AElevatorShaftController::OnCombatEvent(FGameplayTag Room, ERoomCombatEvent Event, int32 Phase, int32 Generation)
{
	if (Room != CombatRoomTag || Generation != static_cast<int32>(Motion.Generation)
		|| Phase != CombatContext.CombatPhaseIndex)
	{
		return;
	}
	if (Event == ERoomCombatEvent::Cancelled)
	{
		if (Motion.State != EElevatorShaftState::WaitingForPair && Motion.State != EElevatorShaftState::Completed)
		{
			ResetExecution(Motion.Generation);
		}
		return;
	}
	if (Event == ERoomCombatEvent::RoomCleared && (Motion.State == EElevatorShaftState::RisingCombat
		|| Motion.State == EElevatorShaftState::AwaitingSpawn))
	{
		BeginExit();
	}
}

void AElevatorShaftController::SetState(EElevatorShaftState State)
{
	Motion.State = State;
	if (State != EElevatorShaftState::WaitingForPair)
	{
		GetWorldTimerManager().ClearTimer(EntryCheckTimer);
	}
	OnRep_Motion();
	ForceNetUpdate();
}

void AElevatorShaftController::SetEntranceBlocked(bool bBlocked)
{
	EntranceBlocker->SetCollisionEnabled(bBlocked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

double AElevatorShaftController::ServerTime() const
{
	return Clock.IsValid() ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

double AElevatorShaftController::CurrentDistance() const
{
	return Motion.State == EElevatorShaftState::RisingCombat
		? FMath::Max(0.0, ServerTime() - Motion.StartServerTime) * Motion.Speed : Motion.StoppedDistance;
}

double AElevatorShaftController::CalculateLoopOffset(double Distance, int32 Slot, int32 Count, double Height)
{
	if (Count < 1 || Slot < 0 || Slot >= Count || Height <= 0 || !FMath::IsFinite(Distance)
		|| !FMath::IsFinite(Height) || !FMath::IsFinite(Count * Height))
	{
		return 0;
	}
	const double Offset = FMath::Max(0.0, Distance) - (Slot + 1) * Height;
	return Offset < Height ? Offset : FMath::Fmod(Offset - Height, Count * Height) - (Count - 1) * Height;
}

void AElevatorShaftController::InitializeVisualPool()
{
	if (GetNetMode() == NM_DedicatedServer || !LoopSections.IsEmpty() || !LoopSectionClass || !EntranceSection)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < LoopSectionCount; ++Index)
	{
		AElevatorShaftSection* Section = GetWorld()->SpawnActor<AElevatorShaftSection>(LoopSectionClass, Motion.EntranceTransform, Params);
		if (!Section)
		{
			continue;
		}
		if (Section->HasEntranceDoor() || !FMath::IsNearlyEqual(Section->GetSectionHeight(), EntranceSection->GetSectionHeight()))
		{
			UE_LOG(LogElevatorShaft, Error, TEXT("%s: loop sections must have matching height and no gameplay door."), *GetName());
			Section->Destroy();
			bConfigurationValid = false;
			break;
		}
		LoopSections.Add(Section);
	}
	UpdateVisuals();
}

void AElevatorShaftController::UpdateVisuals()
{
	if (!EntranceSection)
	{
		return;
	}
	const double Distance = CurrentDistance();
	if (Distance > 0 && (!EntranceDoor || !EntranceDoor->IsPresentationOnly()))
	{
		// 두 Actor의 복제 순서는 보장되지 않는다. 문 충돌 해제를 받은 뒤에만 세트를 움직인다.
		// 늦은 relevancy로 이미 정지한 상태를 받아도 이 짧은 준비 대기로 외형을 복구한다.
		if (!GetWorldTimerManager().IsTimerActive(VisualReadyTimer))
		{
			GetWorldTimerManager().SetTimer(VisualReadyTimer, this, &ThisClass::UpdateVisuals, 0.05f, true);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(VisualReadyTimer);
	const double Height = EntranceSection->GetSectionHeight();
	FTransform Transform = Motion.EntranceTransform;
	Transform.AddToTranslation(FVector(0, 0, Distance));
	EntranceSection->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	EntranceSection->SetSectionVisible(Distance < Height);
	if (EntranceDoor)
	{
		// 문이 Section의 초기 캐시보다 늦게 복제된 경우에도 입구 세트와 함께 숨긴다.
		EntranceDoor->SetActorHiddenInGame(Distance >= Height);
	}
	for (int32 Index = 0; Index < LoopSections.Num(); ++Index)
	{
		Transform = Motion.EntranceTransform;
		Transform.AddToTranslation(FVector(0, 0, CalculateLoopOffset(Distance, Index, LoopSectionCount, Height)));
		LoopSections[Index]->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AElevatorShaftController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateVisuals();
}

void AElevatorShaftController::OnRep_Motion()
{
	if (!bInitialized)
	{
		return;
	}
	SetEntranceBlocked(Motion.State >= EElevatorShaftState::Intro);
	if (GetNetMode() != NM_DedicatedServer)
	{
		InitializeVisualPool();
		UpdateVisuals();
	}
	// 매 프레임 필요한 것은 시각 위치 계산뿐이다. 게임플레이는 이벤트/타이머로만 진행한다.
	SetActorTickEnabled(GetNetMode() != NM_DedicatedServer && Motion.State == EElevatorShaftState::RisingCombat);
	ShaftStateChanged(Motion.State);
}

void AElevatorShaftController::OnReload(uint32 Generation)
{
	ResetExecution(Generation);
}

void AElevatorShaftController::ResetExecution(uint32 Generation)
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(RetryTimer);
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	GetWorldTimerManager().ClearTimer(EntryCheckTimer);
	GetWorldTimerManager().ClearTimer(VisualReadyTimer);
	ReleaseTransition();
	Motion.ExecutionId = FGuid::NewGuid();
	Motion.Generation = Generation;
	Motion.StartServerTime = 0;
	Motion.StoppedDistance = 0;
	CombatContext = FRoomCombatTriggerContext();
	bCombatRequested = false;
	bCloseRejected = false;
	bRevealRequested = false;
	if (EntranceSection)
	{
		EntranceSection->SetActorTransform(Motion.EntranceTransform);
		EntranceSection->SetSectionVisible(true);
	}
	if (EntranceDoor)
	{
		EntranceDoor->ResetToInitialState();
	}
	SetEntranceBlocked(false);
	SetState(EElevatorShaftState::WaitingForPair);
}

void AElevatorShaftController::BeginExit()
{
	if (!HasAuthority() || Motion.State == EElevatorShaftState::Completed || InputBlockId.IsValid())
	{
		return;
	}
	Motion.StoppedDistance = CurrentDistance();
	SetState(EElevatorShaftState::TransitionOut);
	if (!GameMode.IsValid() || GameMode->IsGameOverSelectionActive()
		|| !GameMode->IsPairGameplayReady() || !CachePair())
	{
		RetryExit();
		return;
	}
	APartnerPlayerController* Partner = Cast<APartnerPlayerController>(PartnerController.Get());
	if (!Partner || !Partner->RestorePartnerForWorldTransition())
	{
		RetryExit();
		return;
	}
	AFirstPersonCharacter* ShooterPawn = Cast<AFirstPersonCharacter>(ShooterController->GetPawn());
	AFirstPersonCharacter* PartnerPawn = Cast<AFirstPersonCharacter>(PartnerController->GetPawn());
	if (!ShooterPawn || !PartnerPawn)
	{
		RetryExit();
		return;
	}

	// 최종 전투 저장은 반드시 출구 이동 이전 좌표로 확정한다. 빙의 중 실패한 저장도 여기서 재시도한다.
	if (!GameMode.IsValid() || (!GameMode->IsRestoringClearedEncounter(CombatRoomTag)
		&& !GameMode->CommitCombatPhaseCheckpoint(CombatRoomTag, INDEX_NONE, true, Motion.Generation)))
	{
		RetryExit();
		return;
	}
	InputBlockId = FGuid::NewGuid();
	BlockedShooter = ShooterPawn;
	BlockedPartner = PartnerPawn;
	if (!ShooterPawn->AcquireSuitTransitionBlock(InputBlockId)
		|| !PartnerPawn->AcquireSuitTransitionBlock(InputBlockId))
	{
		ReleaseTransition();
		RetryExit();
		return;
	}
	ShooterPawn->OnSuitTransitionParticipantInvalidated.AddUObject(this, &ThisClass::OnParticipantInvalidated);
	PartnerPawn->OnSuitTransitionParticipantInvalidated.AddUObject(this, &ThisClass::OnParticipantInvalidated);
	RequestFade(false);
}

void AElevatorShaftController::RequestFade(bool bReveal)
{
	bRevealRequested = bReveal;
	bShooterFadeReady = false;
	bPartnerFadeReady = false;
	FadeRequestTime = ServerTime();
	const float Duration = bReveal ? FadeInDuration : FadeOutDuration;
	GetWorldTimerManager().SetTimer(FadeTimeoutTimer, this, &ThisClass::AbortTransition,
		FMath::Max(TransitionTimeout, Duration + 2.f), false);
	ShooterController->SendElevatorFadeFromServer(this, Motion.ExecutionId, bReveal, Duration);
	PartnerController->SendElevatorFadeFromServer(this, Motion.ExecutionId, bReveal, Duration);
}

void AElevatorShaftController::NotifyClientFadeFinished(AFirstPersonPlayerController* Player,
	const FGuid& ExecutionId, bool bReveal)
{
	if (!HasAuthority() || Motion.State != EElevatorShaftState::TransitionOut
		|| !InputBlockId.IsValid() || ExecutionId != Motion.ExecutionId || bReveal != bRevealRequested
		|| (Arena.IsValid() && Arena->GetGameplayGeneration() != Motion.Generation))
	{
		return;
	}
	const double MinDuration = bReveal ? FadeInDuration : FadeOutDuration;
	if (Player == ShooterController.Get())
	{
		bShooterFadeReady = true;
	}
	else if (Player == PartnerController.Get())
	{
		bPartnerFadeReady = true;
	}
	else
	{
		return;
	}
	if (!bShooterFadeReady || !bPartnerFadeReady)
	{
		return;
	}
	const double Remaining = MinDuration - (ServerTime() - FadeRequestTime);
	if (Remaining > KINDA_SMALL_NUMBER)
	{
		// 이미 어두운 화면은 PP가 더 빨리 끝날 수 있다. 응답은 보관하되 서버 최소 시간까지 기다린다.
		GetWorldTimerManager().SetTimer(FadeMinimumTimer, this, &ThisClass::FinishFadeWhenReady, static_cast<float>(Remaining), false);
		return;
	}
	FinishFadeWhenReady();
}

void AElevatorShaftController::FinishFadeWhenReady()
{
	if (Motion.State != EElevatorShaftState::TransitionOut || !InputBlockId.IsValid()
		|| !bShooterFadeReady || !bPartnerFadeReady)
	{
		return;
	}
	if (!GameMode.IsValid() || GameMode->IsGameOverSelectionActive() || !GameMode->IsPairGameplayReady()
		|| (Arena.IsValid() && Arena->GetGameplayGeneration() != Motion.Generation))
	{
		// 암전 중 전멸해도 Pawn은 남아 있을 수 있다. 기존 GameOver/리로드가 진행을 인계받는다.
		AbortTransition();
		return;
	}
	if (!BlockedShooter.IsValid() || !BlockedPartner.IsValid()
		|| !ShooterController.IsValid() || !PartnerController.IsValid()
		|| ShooterController->GetPawn() != BlockedShooter.Get() || PartnerController->GetPawn() != BlockedPartner.Get())
	{
		AbortTransition();
		return;
	}
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	if (bRevealRequested)
	{
		FinishExit();
	}
	else
	{
		TryTeleportPair();
	}
}

void AElevatorShaftController::OnParticipantInvalidated(AFirstPersonCharacter* Pawn)
{
	AbortTransition();
}

bool AElevatorShaftController::ValidateExit(AFirstPersonCharacter* Pawn, const FTransform& Target) const
{
	if (!Pawn || !ExitRoom || Target.ContainsNaN() || !ExitRoom->ContainsWorldLocation(Target.GetLocation()))
	{
		UE_LOG(LogElevatorShaft, Warning,
			TEXT("%s: exit room check failed Pawn=%s Room=%s Target=%s InvalidTransform=%d"),
			*GetName(), *GetNameSafe(Pawn), *GetNameSafe(ExitRoom),
			*Target.GetLocation().ToString(), Target.ContainsNaN() ? 1 : 0);
		return false;
	}
	const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
	if (!Capsule)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorExit), false, Pawn);
	if (ShooterController.IsValid())
	{
		Params.AddIgnoredActor(ShooterController->GetPawn());
	}
	if (PartnerController.IsValid())
	{
		Params.AddIgnoredActor(PartnerController->GetPawn());
	}
	// 실패 시 실제 차단 컴포넌트를 남긴다. 위치를 임의 보정하거나 안전 검증을 생략하지 않는다.
	TArray<FOverlapResult> Overlaps;
	const bool bBlocked = GetWorld()->OverlapMultiByProfile(Overlaps, Target.GetLocation(), Target.GetRotation(),
		Capsule->GetCollisionProfileName(), FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
			Capsule->GetScaledCapsuleHalfHeight()), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (!Overlap.bBlockingHit) continue;
		UE_LOG(LogElevatorShaft, Warning,
			TEXT("%s: exit collision blocked Pawn=%s Target=%s Radius=%.2f HalfHeight=%.2f Actor=%s Component=%s"),
			*GetName(), *GetNameSafe(Pawn), *Target.GetLocation().ToString(),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
			*GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
	}
	return !bBlocked;
}

void AElevatorShaftController::TryTeleportPair()
{
	AFirstPersonCharacter* Shooter = ShooterController.IsValid() ? Cast<AFirstPersonCharacter>(ShooterController->GetPawn()) : nullptr;
	AFirstPersonCharacter* Partner = PartnerController.IsValid() ? Cast<AFirstPersonCharacter>(PartnerController->GetPawn()) : nullptr;
	if (!Shooter || !Partner || !ShooterExitTarget || !PartnerExitTarget)
	{
		AbortTransition();
		return;
	}
	const FTransform ShooterTarget = ShooterExitTarget->GetActorTransform();
	const FTransform PartnerTarget = PartnerExitTarget->GetActorTransform();
	const float Separation = Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ Partner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	// 양쪽을 모두 검사해 한쪽 실패가 다른 쪽의 진단을 가리지 않게 한다.
	const bool bShooterExitValid = ValidateExit(Shooter, ShooterTarget);
	const bool bPartnerExitValid = ValidateExit(Partner, PartnerTarget);
	const double TargetDistanceSquared = FVector::DistSquared(ShooterTarget.GetLocation(), PartnerTarget.GetLocation());
	if (TargetDistanceSquared < FMath::Square(Separation))
	{
		UE_LOG(LogElevatorShaft, Warning, TEXT("%s: exit targets too close Distance=%.2f Required=%.2f"),
			*GetName(), FMath::Sqrt(TargetDistanceSquared), Separation);
	}
	if (!bShooterExitValid || !bPartnerExitValid || TargetDistanceSquared < FMath::Square(Separation))
	{
		UE_LOG(LogElevatorShaft, Warning, TEXT("%s: exit targets unavailable or blocked; transition will retry."), *GetName());
		AbortTransition();
		return;
	}
	const FTransform PreviousShooter = Shooter->GetActorTransform();
	const FTransform PreviousPartner = Partner->GetActorTransform();
	const FRotator PreviousShooterView = ShooterController->GetControlRotation();
	const FRotator PreviousPartnerView = PartnerController->GetControlRotation();
	// 두 목적지를 먼저 검증하고 실제 이동도 모두 성공한 경우에만 Room/화면 전환을 확정한다.
	const bool bShooterMoved = Shooter->TeleportTo(ShooterTarget.GetLocation(), ShooterTarget.Rotator());
	const bool bPartnerMoved = bShooterMoved && Partner->TeleportTo(PartnerTarget.GetLocation(), PartnerTarget.Rotator());
	if (!bShooterMoved || !bPartnerMoved
		|| !ExitRoom->ContainsWorldLocation(Shooter->GetActorLocation())
		|| !ExitRoom->ContainsWorldLocation(Partner->GetActorLocation()))
	{
		Shooter->TeleportTo(PreviousShooter.GetLocation(), PreviousShooter.Rotator(), false, true);
		Partner->TeleportTo(PreviousPartner.GetLocation(), PreviousPartner.Rotator(), false, true);
		ShooterController->SetControlRotation(PreviousShooterView);
		PartnerController->SetControlRotation(PreviousPartnerView);
		if (EntryRoom)
		{
			EntryRoom->RefreshOverlappingRoomAssignments();
		}
		AbortTransition();
		return;
	}
	AFirstPersonCharacter* Pawns[] = {Shooter, Partner};
	AFirstPersonPlayerController* Controllers[] = {ShooterController.Get(), PartnerController.Get()};
	const FRotator Views[] = {ShooterTarget.Rotator(), PartnerTarget.Rotator()};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Pawns[Index]->GetCharacterMovement()->StopMovementImmediately();
		Controllers[Index]->SetControlRotation(Views[Index]);
		Controllers[Index]->ClientSetRotation(Views[Index], true);
		if (URoomTagComponent* Room = Pawns[Index]->GetRoomTagComp())
		{
			Room->ClearRuntimeRoomAssignment();
			Room->EnterRoom(ExitRoom);
		}
		Pawns[Index]->ForceNetUpdate();
	}
	ExitRoom->RefreshOverlappingRoomAssignments();
	RequestFade(true);
}

void AElevatorShaftController::FinishExit()
{
	ReleaseTransition();
	SetState(EElevatorShaftState::Completed);
}

void AElevatorShaftController::ReleaseTransition()
{
	const FGuid PreviousBlockId = InputBlockId;
	InputBlockId.Invalidate();
	AFirstPersonCharacter* Pawns[] = {BlockedShooter.Get(), BlockedPartner.Get()};
	for (AFirstPersonCharacter* Pawn : Pawns)
	{
		if (!Pawn)
		{
			continue;
		}
		Pawn->OnSuitTransitionParticipantInvalidated.RemoveAll(this);
		Pawn->ReleaseSuitTransitionBlock(PreviousBlockId);
	}
	BlockedShooter.Reset();
	BlockedPartner.Reset();
	AFirstPersonPlayerController* Controllers[] = {ShooterController.Get(), PartnerController.Get()};
	for (AFirstPersonPlayerController* Player : Controllers)
	{
		if (!Player)
		{
			continue;
		}
		Player->ClearElevatorFadeFromServer(this, Motion.ExecutionId);
	}
}

void AElevatorShaftController::AbortTransition()
{
	if (!HasAuthority() || Motion.State != EElevatorShaftState::TransitionOut)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	const bool bAlreadyMoved = bRevealRequested;
	ReleaseTransition();
	Motion.ExecutionId = FGuid::NewGuid();
	if (bAlreadyMoved)
	{
		// 이동이 끝난 뒤 화면 응답이 끊기면 잠금을 풀고 정리한다. 같은 출구 이동을 중복 실행하지 않는다.
		SetState(EElevatorShaftState::Completed);
	}
	else
	{
		RetryExit();
	}
}

void AElevatorShaftController::RetryExit()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().SetTimer(RetryTimer, this, &ThisClass::BeginExit, 1.f, false);
}

void AElevatorShaftController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AElevatorShaftController, Motion);
	DOREPLIFETIME(AElevatorShaftController, EntranceDoor);
}

#if WITH_EDITOR
EDataValidationResult AElevatorShaftController::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	// BP 클래스에는 레벨 인스턴스 참조가 없다. 배치된 Controller에서 연결 관계를 검사한다.
	if (IsTemplate())
	{
		return Parent;
	}
	FString Error;
	if (!ValidateConfiguration(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
