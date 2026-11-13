#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "Explosion/ExplosiveProp.h"
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
#include "Components/AudioComponent.h"
#include "Curves/CurveFloat.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	RootComponent->SetMobility(EComponentMobility::Movable);
	EntranceBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("EntranceBlocker"));
	EntranceBlocker->SetupAttachment(RootComponent);
	EntranceBlocker->SetBoxExtent(FVector(30, 150, 150));
	EntranceBlocker->SetCollisionProfileName(TEXT("BlockAll"));
	EntranceBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EntranceBlocker->SetGenerateOverlapEvents(false);
	EntranceBlocker->SetCanEverAffectNavigation(false);
	ArrivalBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("ArrivalBlocker"));
	ArrivalBlocker->SetupAttachment(RootComponent);
	ArrivalBlocker->SetBoxExtent(FVector(30, 150, 150));
	ArrivalBlocker->SetCollisionProfileName(TEXT("BlockAll"));
	ArrivalBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ArrivalBlocker->SetGenerateOverlapEvents(false);
	ArrivalBlocker->SetCanEverAffectNavigation(false);
}

void AElevatorShaftController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ResolveEntranceActors();
	ResolveArrivalActors();
	UpdateBlockerVisibility();
}

void AElevatorShaftController::UpdateBlockerVisibility()
{
	EntranceBlocker->ShapeColor = FColor::Orange;
	ArrivalBlocker->ShapeColor = FColor::Cyan;
	for (UBoxComponent* Blocker : { EntranceBlocker.Get(), ArrivalBlocker.Get() })
	{
		Blocker->bDrawOnlyIfSelected = false;
		Blocker->SetLineThickness(4.0f);
		Blocker->SetVisibility(true);
		Blocker->SetHiddenInGame(!bShowBlockers);
		Blocker->MarkRenderStateDirty();
	}
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

AInteractableDoor* AElevatorShaftController::FindSwitchDoor(bool bArrival, FString& OutError, bool& bOutPendingConstruction) const
{
	OutError.Reset();
	bOutPendingConstruction = false;
	const AElevatorShaftSection* Section = bArrival ? ArrivalSection.Get() : FindEntranceSection();
	if (!bArrival)
	{
		int32 SectionCount = 0;
		TInlineComponentArray<UChildActorComponent*> ChildComponents;
		GetComponents(ChildComponents, false);
		for (const UChildActorComponent* Component : ChildComponents)
		{
			if (Component->GetChildActorClass()
				&& Component->GetChildActorClass()->IsChildOf(AElevatorShaftSection::StaticClass()))
			{
				++SectionCount;
				bOutPendingConstruction |= !Component->GetChildActor();
			}
		}
		if (SectionCount != 1)
		{
			OutError = TEXT("Controller requires exactly one direct Entrance Section Child Actor.");
			bOutPendingConstruction = false;
			return nullptr;
		}
		if (bOutPendingConstruction)
		{
			OutError = TEXT("Entrance Section Child Actor is not constructed yet.");
			return nullptr;
		}
	}
	if (!IsValid(Section) || Section->IsActorBeingDestroyed() || Section->GetWorld() != GetWorld())
	{
		OutError = FString::Printf(TEXT("%s Section is missing, invalid, destroying or in another world."),
			bArrival ? TEXT("Arrival") : TEXT("Entrance"));
		return nullptr;
	}
	FString SectionError;
	AInteractableDoor* Door = Section->FindCurrentDoor(SectionError, bOutPendingConstruction);
	if (!Door)
	{
		OutError = FString::Printf(TEXT("Section=%s: %s"), *Section->GetPathName(), *SectionError);
	}
	return Door;
}

namespace
{
	// 문 이동음은 서버에서만 재생되므로 서버가 가진 엘리베이터 문에 전용 컨텍스트를 지정한다.
	// 레벨에서 따로 지정한 컨텍스트가 있으면 그대로 둔다.
	void ApplyElevatorDoorAudioContext(AInteractableDoor* Door)
	{
		// 에디터 OnConstruction에서도 Resolve가 불리므로 게임 월드에서만 적용해 레벨 저장값을 건드리지 않는다.
		if (!Door || !Door->HasAuthority() || !Door->GetWorld() || !Door->GetWorld()->IsGameWorld()
			|| !Door->DoorMovementAudioContextTags.IsEmpty()) return;
		const FGameplayTag ElevatorDoorTag = FGameplayTag::RequestGameplayTag(
			TEXT("Audio.Context.Object.Elevator.door"), false);
		if (ElevatorDoorTag.IsValid())
		{
			Door->DoorMovementAudioContextTags.AddTag(ElevatorDoorTag);
		}
	}
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
	ApplyElevatorDoorAudioContext(EntranceDoor);
}

void AElevatorShaftController::ResolveArrivalActors()
{
	// 실제 문은 서버 참조를 복제한다. 에디터에서는 Section 지정 즉시 연결 상태를 보여준다.
	if (!HasAuthority()) return;
	ArrivalDoor = nullptr;
	if (ArrivalSection)
	{
		ArrivalSection->RefreshChildActors();
		ArrivalDoor = ArrivalSection->GetEntranceDoor();
	}
	ApplyElevatorDoorAudioContext(ArrivalDoor);
}

void AElevatorShaftController::BeginPlay()
{
	Super::BeginPlay();
	// 기존 BP에 저장된 초기 OFF도 보완한다. 출구는 도착 완료가 확정될 때만 연다.
	ArrivalBlocker->SetCollisionEnabled(Motion.State == EElevatorShaftState::Completed
		? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	UpdateBlockerVisibility();
	// Section은 각 머신의 Child Actor를 사용하고, 실제 문 참조는 서버 복제로도 보완한다.
	ResolveEntranceActors();
	ResolveArrivalActors();
	Clock = GetWorld()->GetGameState();
	DepartureControllerTransform = GetActorTransform();
	if (HasAuthority()) InitializeTransportProps();
	ControllerSectionOffset = DepartureControllerTransform.GetRelativeTransform(
		EntranceSection ? EntranceSection->GetActorTransform() : DepartureControllerTransform);
	// 암전 중에는 바닥/고정 외벽도 함께 옮긴다. 기존 BP에 저장된 Static 설정도 보완한다.
	TInlineComponentArray<USceneComponent*> SceneComponents;
	GetComponents(SceneComponents, true);
	RootComponent->SetMobility(EComponentMobility::Movable);
	for (USceneComponent* Component : SceneComponents)
	{
		Component->SetMobility(EComponentMobility::Movable);
	}
	Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	FString ErrorForProfiles;
	bProfilesReady = BuildMotionProfiles(ErrorForProfiles);
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
	Motion.AccelerationSeconds = AccelerationDuration;
	Motion.DecelerationSeconds = DecelerationDuration;
	if (ArrivalSection)
	{
		Motion.ArrivalTransform = ArrivalSection->GetActorTransform();
	}
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
	Combat->OnCombatLifecycleEvent.AddUObject(this, &ThisClass::OnCombatEvent);
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
	// 아래 구조물 복귀 경로가 해제 중인 별도 레벨 액터까지 옮기지 않도록 먼저 기록을 버린다.
	TransportedPropTransforms.Reset();
	for (const TWeakObjectPtr<AExplosiveProp>& Prop : RegisteredTransportProps)
	{
		if (Prop.IsValid()) Prop->UnregisterElevatorTransport(this);
	}
	RegisteredTransportProps.Reset();
	StopCollisionDiagnostics();
	GetWorldTimerManager().ClearTimer(CombatStartTimer);
	GetWorldTimerManager().ClearAllTimersForObject(this);
	SetControllerAtArrival(false);
	RollbackPair();
	if (IsValid(ArrivalSection))
	{
		ArrivalSection->SetActorTransform(Motion.ArrivalTransform);
		ArrivalSection->SetSectionVisible(true);
	}
	if (HasAuthority() && IsValid(ArrivalDoor)) ArrivalDoor->SetPresentationOnly(false);
	ReleaseTransition();
	StopMotionAudio();
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
		Combat->ReleaseEncounterCheckpointHold(CombatRoomTag, Motion.ExecutionId);
		Combat->OnCombatLifecycleEvent.RemoveAll(this);
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
		|| !FMath::IsFinite(CombatStartDelaySeconds) || CombatStartDelaySeconds < 0
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
	if (!ArrivalSection || ArrivalSection == Section || ArrivalSection->GetWorld() != GetWorld()
		|| ArrivalSection->GetParentActor() || !ArrivalSection->GetActorScale3D().Equals(FVector::OneVector)
		|| !ArrivalSection->GetActorUpVector().Equals(FVector::UpVector)
		|| !FMath::IsNearlyEqual(Section->GetSectionHeight(), ArrivalSection->GetSectionHeight()))
	{
		Error = TEXT("Arrival Section must be a separate level actor with matching height, unit scale and world +Z up.");
		return false;
	}
	TArray<AActor*> ArrivalActors;
	ArrivalSection->GetAllChildActors(ArrivalActors, true);
	const AInteractableDoor* ExitDoor = nullptr;
	int32 ArrivalDoors = 0;
	for (const AActor* Actor : ArrivalActors)
	{
		if (const AInteractableDoor* Door = Cast<AInteractableDoor>(Actor))
		{
			ExitDoor = Door;
			++ArrivalDoors;
		}
	}
	const FName EffectiveArrivalDoorId = !ArrivalDoorId.IsNone() ? ArrivalDoorId
		: (ExitDoor ? ExitDoor->DoorId : NAME_None);
	if (ArrivalDoors != 1 || !ExitDoor || EffectiveArrivalDoorId.IsNone()
		|| EntranceDoorId.IsNone() || EffectiveArrivalDoorId == EntranceDoorId)
	{
		Error = TEXT("Arrival Section requires one door and a non-empty ArrivalDoorId (or door DoorId), distinct from EntranceDoorId.");
		return false;
	}
	for (TActorIterator<AElevatorShaftController> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->ArrivalSection == ArrivalSection)
		{
			Error = TEXT("Arrival Section must belong to only one Elevator Controller.");
			return false;
		}
	}
	FElevatorShaftSpeedProfile Acceleration;
	FElevatorShaftSpeedProfile Deceleration;
	if (!Acceleration.Build(AccelerationCurve, true, Error) || !Deceleration.Build(DecelerationCurve, false, Error))
	{
		return false;
	}
	if (!FMath::IsFinite(AccelerationDuration) || AccelerationDuration <= 0
		|| !FMath::IsFinite(DecelerationDuration) || DecelerationDuration <= 0
		|| !FMath::IsFinite(ArrivalVisibleDuration) || ArrivalVisibleDuration < 0.1f)
	{
		Error = TEXT("Motion durations must be finite and positive; arrival visible duration must be at least 0.1 seconds.");
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
		bRestoreCleared = true;
		SetEntranceBlocked(true);
		BeginExit();
		return;
	}
	if (bCloseRejected || !IsPairInside() || !Combat.IsValid() || !Combat->IsRoomRegistered(CombatRoomTag))
	{
		return;
	}
	if (!Combat->IsElevatorEncounter(CombatRoomTag))
	{
		UE_LOG(LogElevatorShaft, Error, TEXT("%s requires an ExternalTrigger phase followed only by Automatic phases with valid spawn waves."), *GetName());
		return;
	}
	if (!Combat->CreateTriggerContext(this, CombatRoomTag, FGameplayTag(), CombatContext))
	{
		return;
	}
	// 안전 재개방/닫힘 거부 뒤 같은 Room에 재진입해도 이전 시도의 연출 콜백을 재사용하지 않는다.
	Motion.ExecutionId = FGuid::NewGuid();
	if (!Combat->AcquireEncounterCheckpointHold(CombatContext, Motion.ExecutionId))
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
		if (Combat.IsValid()) Combat->ReleaseEncounterCheckpointHold(CombatRoomTag, Motion.ExecutionId);
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
		BeginCombatStartDelay();
	}
}

void AElevatorShaftController::FinishIntro(FGuid ExecutionId)
{
	if (HasAuthority() && ExecutionId == Motion.ExecutionId && Motion.State == EElevatorShaftState::Intro
		&& (!Arena.IsValid() || Arena->GetGameplayGeneration() == Motion.Generation))
	{
		BeginCombatStartDelay();
	}
}

void AElevatorShaftController::BeginCombatStartDelay()
{
	if (!HasAuthority() || (Motion.State != EElevatorShaftState::ClosingEntrance
		&& Motion.State != EElevatorShaftState::Intro))
	{
		return;
	}
	if (!FMath::IsFinite(CombatStartDelaySeconds) || CombatStartDelaySeconds < 0)
	{
		ResetExecution(Motion.Generation);
		UE_LOG(LogElevatorShaft, Error, TEXT("%s: CombatStartDelaySeconds must be finite and non-negative."), *GetName());
		return;
	}
	const FGuid ExecutionId = Motion.ExecutionId;
	const uint32 Generation = Motion.Generation;
	SetState(EElevatorShaftState::WaitingForCombatStart);
	// BP 상태 알림 중 Reset됐다면 이전 운행의 타이머를 만들지 않는다.
	if (Motion.ExecutionId != ExecutionId || Motion.State != EElevatorShaftState::WaitingForCombatStart) return;
	if (CombatStartDelaySeconds == 0.0f)
	{
		StartCombat();
		return;
	}
	GetWorldTimerManager().SetTimer(CombatStartTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, ExecutionId, Generation]()
		{
			if (Motion.ExecutionId == ExecutionId && Motion.Generation == Generation
				&& Motion.State == EElevatorShaftState::WaitingForCombatStart
				&& (!Arena.IsValid() || Arena->GetGameplayGeneration() == Generation))
			{
				StartCombat();
			}
		}), CombatStartDelaySeconds, false);
}

void AElevatorShaftController::StartCombat()
{
	if (!HasAuthority() || !Combat.IsValid() || bStartingCombat || bCombatRequested)
	{
		return;
	}
	// 준비 부족으로 시작이 거부된 재시도도 같은 등록/운행에만 저장 보류를 다시 획득한다.
	if (!Combat->AcquireEncounterCheckpointHold(CombatContext, Motion.ExecutionId))
	{
		ResetExecution(Motion.Generation);
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
		// 시작 자체가 거부되면 봉쇄/저장 보류도 되돌린다. 준비 알림이나 새 진입에서 다시 시도한다.
		ResetExecution(Motion.Generation);
		bCloseRejected = true;
		UE_LOG(LogElevatorShaft, Warning, TEXT("%s: combat start rejected; entry restored until Room readiness changes."), *GetName());
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
		|| Generation != static_cast<int32>(Motion.Generation) || RegistrationId != CombatContext.RoomRegistrationId
		|| !Combat.IsValid() || !Combat->IsEncounterContextCurrent(CombatContext, Motion.ExecutionId))
	{
		return;
	}
	Motion.StartServerTime = ServerTime();
	Motion.StoppedDistance = 0;
	Motion.bStarted = true;
	SetState(EElevatorShaftState::RisingCombat);
	UpdateMotionPhase();
}

void AElevatorShaftController::OnCombatEvent(const FRoomCombatLifecycleEvent& Context)
{
	if (!HasAuthority() || Context.RoomTag != CombatRoomTag
		|| Context.GameplayGeneration != static_cast<int32>(Motion.Generation)
		|| Context.GameplayGeneration != CombatContext.GameplayGeneration
		|| !Context.RoomRegistrationId.IsValid() || Context.RoomRegistrationId != CombatContext.RoomRegistrationId
		|| !Context.ExecutionId.IsValid() || Context.ExecutionId != Motion.ExecutionId)
	{
		return;
	}
	// Cancelled는 Room 제거 후 도착한다. 현재 등록 조회로 이 정리 경로를 막지 않는다.
	if (Context.Event == ERoomCombatEvent::Cancelled)
	{
		if (Motion.State != EElevatorShaftState::WaitingForPair && Motion.State != EElevatorShaftState::Completed)
		{
			ResetExecution(Motion.Generation);
		}
		return;
	}
	if (Context.Event == ERoomCombatEvent::RoomCleared && (Motion.State == EElevatorShaftState::RisingCombat
		|| Motion.State == EElevatorShaftState::AwaitingSpawn)
		&& Combat.IsValid() && Combat->IsEncounterContextCurrent(CombatContext, Motion.ExecutionId)
		&& Combat->GetRoomState(CombatRoomTag) == ERoomCombatState::Cleared)
	{
		Combat->ReleaseEncounterCheckpointHold(CombatRoomTag, Motion.ExecutionId);
		// 완료 순간의 한 층 경계만 예약한다. 중복 RoomCleared는 이 상태에 다시 진입하지 않는다.
		Motion.FinishDistance = FElevatorShaftSpeedProfile::FinishBoundary(CurrentDistance(), EntranceSection->GetSectionHeight());
		SetState(EElevatorShaftState::FinishingCurrentSection);
		UpdateMotionPhase();
	}
}

void AElevatorShaftController::SetState(EElevatorShaftState State)
{
	if (State == EElevatorShaftState::WaitingForPair && Combat.IsValid())
	{
		Combat->ReleaseEncounterCheckpointHold(CombatRoomTag, Motion.ExecutionId);
	}
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

bool AElevatorShaftController::BuildMotionProfiles(FString& Error)
{
	return AccelerationProfile.Build(AccelerationCurve, true, Error)
		&& DecelerationProfile.Build(DecelerationCurve, false, Error);
}

double AElevatorShaftController::DepartureDistance(double Elapsed) const
{
	const double Time = FMath::Max(0.0, Elapsed);
	return Motion.Speed * (Motion.AccelerationSeconds * AccelerationProfile.Integral(Time / Motion.AccelerationSeconds)
		+ FMath::Max(0.0, Time - Motion.AccelerationSeconds));
}

double AElevatorShaftController::ArrivalTravel(double Elapsed) const
{
	const double Time = FMath::Clamp(Elapsed, 0.0, Motion.BrakeStartSeconds + Motion.DecelerationSeconds);
	if (Time <= Motion.BrakeStartSeconds)
	{
		return DepartureDistance(Motion.DepartureElapsed + Time) - DepartureDistance(Motion.DepartureElapsed);
	}
	const double BeforeBrake = DepartureDistance(Motion.DepartureElapsed + Motion.BrakeStartSeconds)
		- DepartureDistance(Motion.DepartureElapsed);
	return BeforeBrake + Motion.Speed * Motion.DecelerationSeconds
		* DecelerationProfile.Integral((Time - Motion.BrakeStartSeconds) / Motion.DecelerationSeconds);
}

double AElevatorShaftController::CurrentDistance() const
{
	if (Motion.bArrival)
	{
		return Motion.bLanded ? Motion.ArrivalDistance
			: ArrivalTravel(Motion.ArrivalStartTime > 0 ? ServerTime() - Motion.ArrivalStartTime : 0);
	}
	return Motion.bStarted ? DepartureDistance(ServerTime() - Motion.StartServerTime) : Motion.StoppedDistance;
}

bool AElevatorShaftController::IsTransitionActive() const
{
	return Motion.State == EElevatorShaftState::TransitionOut || Motion.State == EElevatorShaftState::PreparingArrival
		|| Motion.State == EElevatorShaftState::Arriving || Motion.State == EElevatorShaftState::Decelerating;
}

void AElevatorShaftController::UpdateMotionPhase()
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(MotionPhaseTimer);
	const double Now = ServerTime();
	if (Motion.State == EElevatorShaftState::FinishingCurrentSection)
	{
		if (!Motion.bStarted || CurrentDistance() >= Motion.FinishDistance - UE_DOUBLE_SMALL_NUMBER)
		{
			BeginExit();
			return;
		}
		// 거리가 증가하는 적분 프로파일을 역산한다. 서버는 경계 시각에 한 번만 깨어난다.
		double Low = FMath::Max(0.0, Now - Motion.StartServerTime);
		double High = Low + EntranceSection->GetSectionHeight() / Motion.Speed + Motion.AccelerationSeconds;
		for (int32 Index = 0; Index < 48; ++Index)
		{
			const double Mid = (Low + High) * 0.5;
			if (DepartureDistance(Mid) < Motion.FinishDistance) Low = Mid;
			else High = Mid;
		}
		GetWorldTimerManager().SetTimer(MotionPhaseTimer, this, &ThisClass::UpdateMotionPhase,
			FMath::Max(0.001f, static_cast<float>(Motion.StartServerTime + High - Now)), false);
	}
	else if (Motion.State == EElevatorShaftState::Arriving || Motion.State == EElevatorShaftState::Decelerating)
	{
		const double Elapsed = Now - Motion.ArrivalStartTime;
		if (Elapsed >= Motion.BrakeStartSeconds + Motion.DecelerationSeconds)
		{
			CompleteLanding();
			return;
		}
		const bool bBraking = Elapsed >= Motion.BrakeStartSeconds;
		if (bBraking && Motion.State != EElevatorShaftState::Decelerating) SetState(EElevatorShaftState::Decelerating);
		const double Next = Motion.ArrivalStartTime + Motion.BrakeStartSeconds + (bBraking ? Motion.DecelerationSeconds : 0);
		GetWorldTimerManager().SetTimer(MotionPhaseTimer, this, &ThisClass::UpdateMotionPhase,
			FMath::Max(0.001f, static_cast<float>(Next - Now)), false);
	}
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
	if (Motion.bArrival)
	{
		if (!ArrivalSection || !ArrivalDoor || (!Motion.bLanded && !ArrivalDoor->IsPresentationOnly())) return;
		EntranceSection->SetSectionVisible(false);
		if (EntranceDoor) EntranceDoor->SetActorHiddenInGame(true);
		FTransform Transform = Motion.ArrivalTransform;
		Transform.AddToTranslation(FVector(0, 0, Distance - Motion.ArrivalDistance));
		ArrivalSection->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
		ArrivalSection->SetSectionVisible(true);
		ArrivalDoor->SetActorHiddenInGame(false);
		const double Height = EntranceSection->GetSectionHeight();
		for (int32 Index = 0; Index < LoopSections.Num(); ++Index)
		{
			// 반복 풀은 입구층 위를 덮는다. 아래에는 새 층을 보충하지 않아 도착 문이 한 번만 나타난다.
			Transform = Motion.ArrivalTransform;
			const double Offset = FMath::Fmod(Distance + (Index + 1) * Height, LoopSectionCount * Height) - Height;
			Transform.AddToTranslation(FVector(0, 0, Offset));
			const double EntranceTop = Distance - Motion.ArrivalDistance + Height;
			LoopSections[Index]->SetSectionVisible(Transform.GetLocation().Z - Motion.ArrivalTransform.GetLocation().Z >= EntranceTop - 0.1);
			LoopSections[Index]->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		UpdateMotionAudio();
		return;
	}
	// 취소/Reload의 서버 Transform 변경은 시각 전용 Section에 복제되지 않는다.
	// 출발 좌표계로 돌아온 각 클라이언트도 도착층을 제작된 최종 위치로 복구한다.
	if (ArrivalSection && Motion.ExecutionId.IsValid())
	{
		if (!ArrivalSection->GetActorTransform().Equals(Motion.ArrivalTransform))
		{
			ArrivalSection->SetActorTransform(Motion.ArrivalTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		ArrivalSection->SetSectionVisible(true);
	}
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
		LoopSections[Index]->SetSectionVisible(true);
		LoopSections[Index]->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	UpdateMotionAudio();
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
	// 위치 복제 대신 같은 상태와 도착층 기준으로 서버/클라이언트의 전체 구조물을 맞춘다.
	SetControllerAtArrival(Motion.bArrival);
	SetEntranceBlocked(Motion.State != EElevatorShaftState::WaitingForPair && Motion.State != EElevatorShaftState::ClosingEntrance);
	// 대기/상승과 실패 후 재시도에도 출구를 막는다. 도착 연출 구간에만 한정하지 않는다.
	ArrivalBlocker->SetCollisionEnabled(Motion.State == EElevatorShaftState::Completed
		? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	// 상태 적용 직후 두 고정 장벽을 함께 기록해, 별도 RoomCombatBarrier와 구분한다.
	for (const UBoxComponent* Blocker : { EntranceBlocker.Get(), ArrivalBlocker.Get() })
	{
		UE_LOG(LogElevatorShaft, Log,
			TEXT("[ElevatorBlocker] World=%s Authority=%d NetMode=%d State=%s Component=%s Collision=%s ActorCollision=%d PawnResponse=%s Location=%s"),
			*GetNameSafe(GetWorld()), HasAuthority(), static_cast<int32>(GetNetMode()),
			*UEnum::GetValueAsString(Motion.State), *Blocker->GetPathName(),
			*UEnum::GetValueAsString(Blocker->GetCollisionEnabled()), GetActorEnableCollision(),
			*UEnum::GetValueAsString(Blocker->GetCollisionResponseToChannel(ECC_Pawn)),
			*Blocker->GetComponentLocation().ToString());
	}
	if (PresentationExecutionId != Motion.ExecutionId)
	{
		StopMotionAudio();
		PresentationExecutionId = Motion.ExecutionId;
		PresentedSegments = 0;
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		InitializeVisualPool();
		UpdateVisuals();
	}
	// 매 프레임 필요한 것은 시각 위치 계산뿐이다. 게임플레이는 이벤트/타이머로만 진행한다.
	SetActorTickEnabled(GetNetMode() != NM_DedicatedServer && bProfilesReady
		&& (Motion.State == EElevatorShaftState::RisingCombat || Motion.State == EElevatorShaftState::FinishingCurrentSection
			|| Motion.State == EElevatorShaftState::TransitionOut || Motion.State == EElevatorShaftState::Arriving
			|| Motion.State == EElevatorShaftState::Decelerating));
	if (Motion.bLanded || Motion.State == EElevatorShaftState::WaitingForPair) StopMotionAudio();
	LogCollisionPlacement(TEXT("MotionAppliedBeforeBPEvent"));
	ShaftStateChanged(Motion.State);
	LogCollisionPlacement(TEXT("MotionAppliedAfterBPEvent"));
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
	if (Combat.IsValid()) Combat->ReleaseEncounterCheckpointHold(CombatRoomTag, Motion.ExecutionId);
	GetWorldTimerManager().ClearTimer(CombatStartTimer);
	GetWorldTimerManager().ClearTimer(RetryTimer);
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	GetWorldTimerManager().ClearTimer(EntryCheckTimer);
	GetWorldTimerManager().ClearTimer(VisualReadyTimer);
	GetWorldTimerManager().ClearTimer(MotionPhaseTimer);
	SetControllerAtArrival(false);
	RollbackPair();
	ReleaseTransition();
	StopMotionAudio();
	Motion.bStarted = false;
	Motion.bArrival = false;
	Motion.bLanded = false;
	Motion.ArrivalStartTime = 0;
	Motion.ArrivalDistance = 0;
	Motion.FinishDistance = 0;
	bRestoreCleared = false;
	bPairTeleported = false;
	if (ArrivalSection)
	{
		ArrivalSection->SetActorTransform(Motion.ArrivalTransform);
		ArrivalSection->SetSectionVisible(true);
	}
	if (ArrivalDoor) ArrivalDoor->ResetToInitialState();
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
	const FGuid Execution = Motion.ExecutionId;
	SetState(EElevatorShaftState::TransitionOut);
	if (Motion.ExecutionId != Execution || Motion.State != EElevatorShaftState::TransitionOut) return;
	if (!ArrivalDoor || !Combat.IsValid() || !Combat->AcquireRoomStartHold(ExitRoom->GetRoomTag(), Motion.ExecutionId, this))
	{
		RetryExit();
		return;
	}
	if (!ArrivalDoor->IsPresentationOnly())
	{
		if (ArrivalDoor->IsDoorOpen() || ArrivalDoor->IsDoorMoving())
		{
			ArrivalDoor->TrySetDoorOpen(false);
			RetryExit();
			return;
		}
		if (!ArrivalDoor->SetPresentationOnly(true))
		{
			RetryExit();
			return;
		}
	}
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
	if (!HasAuthority() || !IsValid(Player) || !IsTransitionActive()
		|| !InputBlockId.IsValid() || ExecutionId != Motion.ExecutionId || bReveal != bRevealRequested
		|| (!bReveal && Motion.State != EElevatorShaftState::TransitionOut)
		|| (bReveal && Motion.State != EElevatorShaftState::Arriving && Motion.State != EElevatorShaftState::Decelerating)
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
	if (!IsTransitionActive() || !InputBlockId.IsValid()
		|| (!bRevealRequested && Motion.State != EElevatorShaftState::TransitionOut)
		|| (bRevealRequested && Motion.State != EElevatorShaftState::Arriving && Motion.State != EElevatorShaftState::Decelerating)
		|| !bShooterFadeReady || !bPartnerFadeReady)
	{
		return;
	}
	if (!GameMode.IsValid() || GameMode->IsGameOverSelectionActive() || !GameMode->IsPairGameplayReady()
		|| (Arena.IsValid() && Arena->GetGameplayGeneration() != Motion.Generation))
	{
		// 암전 중 전멸해도 Pawn은 남아 있을 수 있다. 기존 GameOver/리로드가 진행을 인계받는다.
		AbortTransitionWithReason(TEXT("FadeFinishedPairOrGenerationInvalid"));
		return;
	}
	if (!BlockedShooter.IsValid() || !BlockedPartner.IsValid()
		|| !ShooterController.IsValid() || !PartnerController.IsValid()
		|| ShooterController->GetPawn() != BlockedShooter.Get() || PartnerController->GetPawn() != BlockedPartner.Get())
	{
		AbortTransitionWithReason(TEXT("FadeFinishedParticipantChanged"));
		return;
	}
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	if (bRevealRequested)
	{
		if (Motion.bLanded) FinishExit();
	}
	else
	{
		TryTeleportPair();
	}
}

void AElevatorShaftController::OnParticipantInvalidated(AFirstPersonCharacter* Pawn)
{
	AbortTransitionWithReason(TEXT("ParticipantInvalidated"));
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
	// 통의 BeginPlay가 저장된 폭발 상태를 적용한다. 복원 전 상태로 이동 대상을 정하지 않는다.
	if (!AreTransportPropsReady())
	{
		AbortTransitionWithReason(TEXT("TransportPropsNotReady"));
		return;
	}
	AFirstPersonCharacter* Shooter = ShooterController.IsValid() ? Cast<AFirstPersonCharacter>(ShooterController->GetPawn()) : nullptr;
	AFirstPersonCharacter* Partner = PartnerController.IsValid() ? Cast<AFirstPersonCharacter>(PartnerController->GetPawn()) : nullptr;
	if (!Shooter || !Partner || !ShooterExitTarget || !PartnerExitTarget)
	{
		AbortTransitionWithReason(TEXT("MissingPawnOrExitTarget"));
		return;
	}
	const FTransform ShooterTarget = ShooterExitTarget->GetActorTransform();
	const FTransform PartnerTarget = PartnerExitTarget->GetActorTransform();
	const float Separation = Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ Partner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	// 전체 구조물을 먼저 옮겨 바닥/외벽이 도착한 실제 충돌 환경에서 목적지를 검사한다.
	Shooter->SetBase(nullptr);
	Partner->SetBase(nullptr);
	SetControllerAtArrival(true);
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
		AbortTransitionWithReason(TEXT("ExitTargetBlockedOrTooClose"));
		return;
	}
	PreviousShooterTransform = Shooter->GetActorTransform();
	PreviousPartnerTransform = Partner->GetActorTransform();
	PreviousShooterView = ShooterController->GetControlRotation();
	PreviousPartnerView = PartnerController->GetControlRotation();
	// 두 목적지를 먼저 검증하고 실제 이동도 모두 성공한 경우에만 Room/화면 전환을 확정한다.
	const bool bShooterMoved = Shooter->TeleportTo(ShooterTarget.GetLocation(), ShooterTarget.Rotator());
	const bool bPartnerMoved = bShooterMoved && Partner->TeleportTo(PartnerTarget.GetLocation(), PartnerTarget.Rotator());
	if (!bShooterMoved || !bPartnerMoved
		|| !ExitRoom->ContainsWorldLocation(Shooter->GetActorLocation())
		|| !ExitRoom->ContainsWorldLocation(Partner->GetActorLocation()))
	{
		SetControllerAtArrival(false);
		Shooter->TeleportTo(PreviousShooterTransform.GetLocation(), PreviousShooterTransform.Rotator(), false, true);
		Partner->TeleportTo(PreviousPartnerTransform.GetLocation(), PreviousPartnerTransform.Rotator(), false, true);
		ShooterController->SetControlRotation(PreviousShooterView);
		PartnerController->SetControlRotation(PreviousPartnerView);
		if (EntryRoom)
		{
			EntryRoom->RefreshOverlappingRoomAssignments();
		}
		AbortTransitionWithReason(TEXT("TeleportFailedOrOutsideExitRoom"));
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
	bPairTeleported = true;
	ExitRoom->RefreshOverlappingRoomAssignments();
	PrepareArrival();
}

bool AElevatorShaftController::IsTransportPropValid(const AExplosiveProp* Prop) const
{
	return IsValid(Prop) && !Prop->IsActorBeingDestroyed() && Prop->GetWorld() == GetWorld()
		&& Prop->HasAuthority() && Prop->GetRootComponent() && !Prop->GetParentActor()
		&& !Prop->GetAttachParentActor() && !Prop->IsMountedOnSelfDestructDrone();
}

void AElevatorShaftController::InitializeTransportProps()
{
	if (!HasAuthority() || bTransportPropsInitialized) return;
	bTransportPropsInitialized = true;
	TSet<AExplosiveProp*> Seen;
	for (AExplosiveProp* Prop : TransportExplosiveProps)
	{
		if (!IsTransportPropValid(Prop) || Seen.Contains(Prop))
		{
			UE_LOG(LogElevatorShaft, Warning, TEXT("%s: ignored invalid or duplicate transport prop %s."),
				*GetName(), *GetNameSafe(Prop));
			continue;
		}
		Seen.Add(Prop);
		if (!Prop->RegisterElevatorTransport(this))
		{
			UE_LOG(LogElevatorShaft, Warning, TEXT("%s: transport prop %s already belongs to another controller."),
				*GetName(), *GetNameSafe(Prop));
			continue;
		}
		RegisteredTransportProps.Add(Prop);
	}
}

bool AElevatorShaftController::AreTransportPropsReady() const
{
	for (const TWeakObjectPtr<AExplosiveProp>& Prop : RegisteredTransportProps)
	{
		if (IsTransportPropValid(Prop.Get()) && !Prop->HasActorBegunPlay()) return false;
	}
	return true;
}

void AElevatorShaftController::MoveTransportProps(const FVector& Translation)
{
	InitializeTransportProps();
	for (const TWeakObjectPtr<AExplosiveProp>& Entry : RegisteredTransportProps)
	{
		AExplosiveProp* Prop = Entry.Get();
		if (!IsTransportPropValid(Prop) || Prop->IsExploded()) continue;
		const FTransform Original = Prop->GetActorTransform();
		FTransform Target = Original;
		Target.AddToTranslation(Translation);
		Prop->FlushNetDormancy();
		if (Prop->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics))
		{
			TransportedPropTransforms.Emplace(Prop, Original);
		}
		else
		{
			UE_LOG(LogElevatorShaft, Warning, TEXT("%s: failed to move transport prop %s."), *GetName(), *Prop->GetName());
		}
		Prop->ForceNetUpdate();
	}
}

void AElevatorShaftController::RestoreTransportProps()
{
	// 실제로 옮긴 통만 원위치로 되돌린다. 이동 후 폭발했더라도 좌표만 복구하고 폭발 상태는 유지한다.
	for (const TPair<TWeakObjectPtr<AExplosiveProp>, FTransform>& Entry : TransportedPropTransforms)
	{
		if (AExplosiveProp* Prop = Entry.Key.Get(); IsTransportPropValid(Prop))
		{
			Prop->FlushNetDormancy();
			Prop->SetActorTransform(Entry.Value, false, nullptr, ETeleportType::TeleportPhysics);
			Prop->ForceNetUpdate();
		}
	}
	TransportedPropTransforms.Reset();
}

void AElevatorShaftController::SetControllerAtArrival(bool bArrival)
{
	if (bControllerAtArrival == bArrival) return;
	LogCollisionPlacement(bArrival ? TEXT("BeforeTransportToArrival") : TEXT("BeforeTransportToDeparture"));
	bControllerAtArrival = bArrival;
	// Section과 Controller의 원점이 달라도 제작 당시 상대 배치를 보존한다.
	const FTransform Target = bArrival
		? ControllerSectionOffset * Motion.ArrivalTransform : DepartureControllerTransform;
	// 차단막과 고정 외벽/바닥은 제작 당시 월드 방향과 서로의 배치를 유지한 채 이동만 한다.
	// 차단막만 Controller 회전을 상속하면 외벽의 구멍과 어긋나므로 함께 보정한다.
	// 직접 소유한 충돌체만 보정하여 Section 안의 연출 벽과 문은 기존 정렬을 유지한다.
	const FVector Translation = Target.GetLocation() - GetActorLocation();
	// 구조물은 각 머신에서 맞추지만, 독립 폭발통은 서버가 한 번 옮기고 위치 복제로 전달한다.
	if (HasAuthority())
	{
		if (bArrival) MoveTransportProps(Translation);
		else RestoreTransportProps();
	}
	TInlineComponentArray<UPrimitiveComponent*> OwnedPrimitives;
	GetComponents(OwnedPrimitives, false);
	TArray<TPair<UPrimitiveComponent*, FTransform>> FixedCollisionTransforms;
	for (UPrimitiveComponent* Component : OwnedPrimitives)
	{
		FTransform Placement = Component->GetComponentTransform();
		Placement.AddToTranslation(Translation);
		FixedCollisionTransforms.Emplace(Component, Placement);
	}
	SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
	for (const TPair<UPrimitiveComponent*, FTransform>& FixedCollision : FixedCollisionTransforms)
	{
		FixedCollision.Key->SetWorldTransform(FixedCollision.Value, false, nullptr, ETeleportType::TeleportPhysics);
	}
	LogCollisionPlacement(bArrival ? TEXT("AfterTransportToArrival") : TEXT("AfterTransportToDeparture"));
	UE_LOG(LogElevatorShaft, Log, TEXT("[ElevatorTransport] World=%s Authority=%d Arrival=%d Location=%s Rotation=%s"),
		*GetNameSafe(GetWorld()), HasAuthority(), bArrival, *GetActorLocation().ToString(), *GetActorRotation().ToString());
}

void AElevatorShaftController::PrepareArrival()
{
	// 완전 암전 이후에만 풀의 좌표계를 바꾼다. 숨겨진 준비 시간에는 진입 속도를 보관한다.
	Motion.DepartureElapsed = Motion.bStarted ? FMath::Max(0.0, ServerTime() - Motion.StartServerTime) : Motion.AccelerationSeconds;
	Motion.bArrival = true;
	Motion.bLanded = bRestoreCleared;
	Motion.ArrivalStartTime = 0;
	const double RemainingAcceleration = FMath::Max(0.0, Motion.AccelerationSeconds - Motion.DepartureElapsed);
	const double AccelerationDistance = DepartureDistance(Motion.DepartureElapsed + RemainingAcceleration)
		- DepartureDistance(Motion.DepartureElapsed);
	const double BrakeDistance = Motion.Speed * Motion.DecelerationSeconds * DecelerationProfile.TotalIntegral();
	const double Height = EntranceSection->GetSectionHeight();
	// Fade In이 끝난 후에도 움직임이 보이도록 확보하고, 필요한 만큼 정수 층 아래에서 출발한다.
	const double Required = AccelerationDistance + BrakeDistance + Motion.Speed * (FadeInDuration + ArrivalVisibleDuration);
	Motion.ArrivalDistance = FMath::Max(1.0, FMath::CeilToDouble(Required / Height)) * Height;
	Motion.BrakeStartSeconds = RemainingAcceleration
		+ (Motion.ArrivalDistance - AccelerationDistance - BrakeDistance) / Motion.Speed;
	bShooterArrivalReady = false;
	bPartnerArrivalReady = false;
	const FGuid Execution = Motion.ExecutionId;
	SetState(EElevatorShaftState::PreparingArrival);
	if (Motion.ExecutionId != Execution || Motion.State != EElevatorShaftState::PreparingArrival) return;
	GetWorldTimerManager().SetTimer(FadeTimeoutTimer, this, &ThisClass::AbortTransition, TransitionTimeout, false);
	ShooterController->SendElevatorArrivalPreparationFromServer(this, Execution);
	if (Motion.ExecutionId == Execution && Motion.State == EElevatorShaftState::PreparingArrival)
	{
		PartnerController->SendElevatorArrivalPreparationFromServer(this, Execution);
	}
}

bool AElevatorShaftController::IsArrivalPawnReady(const APawn* Pawn) const
{
	return IsValid(Pawn) && ExitRoom && Pawn->GetWorld() == GetWorld()
		&& ExitRoom->ContainsWorldLocation(Pawn->GetActorLocation());
}

FString AElevatorShaftController::DescribeArrivalReadiness(const FGuid& ExecutionId, const APawn* Pawn) const
{
	// 판정을 바꾸지 않고 준비 조건을 나열한다. 좌표는 대기 조건 비교에서 제외해 이동 중 로그 반복을 막는다.
	return FString::Printf(TEXT("State=%s ExecutionMatch=%d Initialized=%d ProfilesReady=%d Arrival=%d ArrivalSection=%s ArrivalDoor=%s PresentationOnly=%d EntranceSection=%s EntranceDoor=%s Clock=%d LoopSections=%d/%d Pawn=%s SameWorld=%d ExitRoom=%s PawnInExitRoom=%d"),
		*UEnum::GetValueAsString(Motion.State), Motion.ExecutionId == ExecutionId, bInitialized, bProfilesReady,
		Motion.bArrival, *GetNameSafe(ArrivalSection), *GetNameSafe(ArrivalDoor),
		ArrivalDoor && ArrivalDoor->IsPresentationOnly(), *GetNameSafe(EntranceSection), *GetNameSafe(EntranceDoor),
		Clock.IsValid(), LoopSections.Num(), LoopSectionCount, *GetNameSafe(Pawn),
		IsValid(Pawn) && Pawn->GetWorld() == GetWorld(), *GetNameSafe(ExitRoom), IsArrivalPawnReady(Pawn));
}

bool AElevatorShaftController::AreArrivalVisualsReady(const FGuid& ExecutionId)
{
	if (!Clock.IsValid()) Clock = GetWorld()->GetGameState();
	if (!bInitialized || !bProfilesReady || Motion.ExecutionId != ExecutionId
		|| Motion.State != EElevatorShaftState::PreparingArrival || !Motion.bArrival
		|| !ArrivalSection || !ArrivalDoor || !EntranceSection || !EntranceDoor
		|| !ArrivalDoor->IsPresentationOnly() || !Clock.IsValid()) return false;
	InitializeVisualPool();
	if (LoopSections.Num() != LoopSectionCount) return false;
	UpdateVisuals();
	return true;
}

void AElevatorShaftController::NotifyClientArrivalReady(AFirstPersonPlayerController* Player, const FGuid& ExecutionId)
{
	if (!HasAuthority() || !IsValid(Player) || Motion.State != EElevatorShaftState::PreparingArrival
		|| Motion.ExecutionId != ExecutionId || !InputBlockId.IsValid()
		|| (Arena.IsValid() && Arena->GetGameplayGeneration() != Motion.Generation))
	{
		UE_LOG(LogElevatorShaft, Warning, TEXT("[ElevatorArrivalAckRejected] Shaft=%s Player=%s Execution=%s CurrentExecution=%s Authority=%d State=%s InputBlockValid=%d GenerationMatch=%d"),
			*GetName(), *GetNameSafe(Player), *ExecutionId.ToString(), *Motion.ExecutionId.ToString(), HasAuthority(),
			*UEnum::GetValueAsString(Motion.State), InputBlockId.IsValid(),
			!Arena.IsValid() || Arena->GetGameplayGeneration() == Motion.Generation);
		return;
	}
	if (Player == ShooterController.Get()) bShooterArrivalReady = true;
	else if (Player == PartnerController.Get()) bPartnerArrivalReady = true;
	else
	{
		UE_LOG(LogElevatorShaft, Warning, TEXT("[ElevatorArrivalAckRejected] Shaft=%s Player=%s Execution=%s Reason=UnknownParticipant Shooter=%s Partner=%s"),
			*GetName(), *GetNameSafe(Player), *ExecutionId.ToString(), *GetNameSafe(ShooterController.Get()), *GetNameSafe(PartnerController.Get()));
		return;
	}
	UE_LOG(LogElevatorShaft, Log, TEXT("[ElevatorArrivalAck] Shaft=%s Player=%s Execution=%s ShooterReady=%d PartnerReady=%d"),
		*GetName(), *GetNameSafe(Player), *ExecutionId.ToString(), bShooterArrivalReady, bPartnerArrivalReady);
	if (bShooterArrivalReady && bPartnerArrivalReady) StartArrival();
}

void AElevatorShaftController::StartArrival()
{
	if (!BlockedShooter.IsValid() || !BlockedPartner.IsValid() || !GameMode.IsValid()
		|| !ShooterController.IsValid() || !PartnerController.IsValid()
		|| !GameMode->IsPairGameplayReady() || GameMode->IsGameOverSelectionActive()
		|| ShooterController->GetPawn() != BlockedShooter.Get() || PartnerController->GetPawn() != BlockedPartner.Get())
	{
		AbortTransitionWithReason(TEXT("StartArrivalParticipantOrPairInvalid"));
		return;
	}
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	Motion.ArrivalStartTime = ServerTime() + 0.25;
	bRevealRequested = true;
	bShooterFadeReady = false;
	bPartnerFadeReady = false;
	FadeRequestTime = Motion.ArrivalStartTime;
	const FGuid Execution = Motion.ExecutionId;
	SetState(EElevatorShaftState::Arriving);
	if (Motion.ExecutionId != Execution || Motion.State != EElevatorShaftState::Arriving) return;
	// 암전 응답 타임아웃은 화면 복구에만 적용한다. 정상 착층 시간과 독립적으로 관리한다.
	GetWorldTimerManager().SetTimer(FadeTimeoutTimer, this, &ThisClass::AbortTransition,
		FMath::Max(TransitionTimeout, FadeInDuration + 2.f) + 0.25f, false);
	ShooterController->SendElevatorScheduledRevealFromServer(this, Motion.ExecutionId, Motion.ArrivalStartTime, FadeInDuration);
	PartnerController->SendElevatorScheduledRevealFromServer(this, Motion.ExecutionId, Motion.ArrivalStartTime, FadeInDuration);
	if (!Motion.bLanded) UpdateMotionPhase();
}

void AElevatorShaftController::CompleteLanding()
{
	if (!HasAuthority() || !Motion.bArrival
		|| (Motion.State != EElevatorShaftState::Arriving && Motion.State != EElevatorShaftState::Decelerating)) return;
	const FGuid Execution = Motion.ExecutionId;
	Motion.bLanded = true;
	OnRep_Motion();
	if (Motion.ExecutionId != Execution || !Motion.bLanded) return;
	ForceNetUpdate();
	// 화면 복구 이후 착층을 기다리는 동안에도 전멸/리로드/페어 교체가 발생할 수 있다.
	if (bShooterFadeReady && bPartnerFadeReady) FinishFadeWhenReady();
}

void AElevatorShaftController::RegisterMotionAudioComponent(UAudioComponent* Component)
{
	if (GetNetMode() != NM_DedicatedServer && IsValid(Component) && Component->GetWorld() == GetWorld()
		&& PresentationExecutionId == Motion.ExecutionId && PresentedSegments != 0 && !Motion.bLanded)
	{
		MotionAudioComponents.AddUnique(Component);
	}
}

void AElevatorShaftController::StopMotionAudio()
{
	for (const TWeakObjectPtr<UAudioComponent>& Component : MotionAudioComponents)
	{
		if (Component.IsValid()) Component->Stop();
	}
	MotionAudioComponents.Reset();
	if (PresentedSegments != 0 && PresentationExecutionId.IsValid())
	{
		// 먼저 비워 BP 종료 이벤트의 재진입에도 같은 실행을 두 번 정리하지 않는다.
		PresentedSegments = 0;
		MotionPresentationStopped(PresentationExecutionId);
	}
}

void AElevatorShaftController::UpdateMotionAudio()
{
	if (GetNetMode() == NM_DedicatedServer || !bProfilesReady || !Motion.bStarted
		|| Motion.bLanded || bRestoreCleared || Motion.State == EElevatorShaftState::PreparingArrival) return;
	const double Now = ServerTime();
	if (Motion.bArrival && Now < Motion.ArrivalStartTime) return;
	const double ArrivalElapsed = Motion.bArrival ? Now - Motion.ArrivalStartTime : 0;
	const double Elapsed = Motion.bArrival ? Motion.DepartureElapsed + ArrivalElapsed : Now - Motion.StartServerTime;
	int32 Segment = Elapsed < Motion.AccelerationSeconds ? 1 : 2;
	double Start = Segment == 1 ? Motion.StartServerTime : Motion.StartServerTime + Motion.AccelerationSeconds;
	if (Motion.bArrival)
	{
		if (ArrivalElapsed >= Motion.BrakeStartSeconds)
		{
			Segment = 3;
			Start = Motion.ArrivalStartTime + Motion.BrakeStartSeconds;
		}
		else
		{
			Start = Motion.ArrivalStartTime + (Segment == 1 ? -Motion.DepartureElapsed
				: Motion.AccelerationSeconds - Motion.DepartureElapsed);
		}
	}
	const uint8 Bit = static_cast<uint8>(1 << Segment);
	if ((PresentedSegments & Bit) != 0) return;
	// 건너뛴 구간도 처리됨으로 기록하여 큰 시각 보정 뒤 과거 시작음을 몰아 재생하지 않는다.
	PresentedSegments |= static_cast<uint8>((1 << (Segment + 1)) - 2);
	const double SegmentElapsed = FMath::Max(0.0, Now - Start);
	if (Segment == 1) OnAccelerationStarted(Motion.ExecutionId, Segment, Start, SegmentElapsed);
	else if (Segment == 2) OnCruiseStarted(Motion.ExecutionId, Segment, Start, SegmentElapsed);
	else OnDecelerationStarted(Motion.ExecutionId, Segment, Start, SegmentElapsed);
}

void AElevatorShaftController::FinishExit()
{
	if (!HasAuthority() || !InputBlockId.IsValid() || !Motion.bLanded
		|| !bShooterFadeReady || !bPartnerFadeReady || !IsTransitionActive()) return;
	const FGuid Execution = Motion.ExecutionId;
	LogCollisionPlacement(TEXT("BeforeArrivalDoorRestore"));
	if (!ArrivalDoor || !ArrivalDoor->SetPresentationOnly(false) || !ArrivalDoor->TrySetDoorOpen(true))
	{
		AbortTransitionWithReason(TEXT("ArrivalDoorOpenFailed"));
		return;
	}
	LogCollisionPlacement(TEXT("AfterArrivalDoorRestore"));
	bPairTeleported = false;
	SetState(EElevatorShaftState::Completed);
	if (Motion.ExecutionId == Execution && Motion.State == EElevatorShaftState::Completed) ReleaseTransition();
}

void AElevatorShaftController::ReleaseTransition()
{
	const FGuid ReleasedExecution = Motion.ExecutionId;
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
		Player->ClearElevatorFadeFromServer(this, ReleasedExecution);
	}
	if (Combat.IsValid()) Combat->ReleaseRoomStartHold(ReleasedExecution);
}

void AElevatorShaftController::RollbackPair()
{
	// 도착 뒤 실패도 성공으로 바꾸지 않는다. 페어와 시각 좌표계를 함께 출발 공간으로 되돌린다.
	if (bPairTeleported)
	{
		AFirstPersonCharacter* Pawns[] = {BlockedShooter.Get(), BlockedPartner.Get()};
		const FTransform Transforms[] = {PreviousShooterTransform, PreviousPartnerTransform};
		AFirstPersonPlayerController* Players[] = {ShooterController.Get(), PartnerController.Get()};
		const FRotator Views[] = {PreviousShooterView, PreviousPartnerView};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Pawns[Index])
			{
				Pawns[Index]->TeleportTo(Transforms[Index].GetLocation(), Transforms[Index].Rotator(), false, true);
				Pawns[Index]->GetCharacterMovement()->StopMovementImmediately();
				if (URoomTagComponent* Room = Pawns[Index]->GetRoomTagComp()) Room->ClearRuntimeRoomAssignment();
			}
			if (Players[Index] && Pawns[Index] && Players[Index]->GetPawn() == Pawns[Index])
			{
				Players[Index]->SetControlRotation(Views[Index]);
				Players[Index]->ClientSetRotation(Views[Index], true);
			}
		}
		if (EntryRoom) EntryRoom->RefreshOverlappingRoomAssignments();
		if (ExitRoom) ExitRoom->RefreshOverlappingRoomAssignments();
	}
	bPairTeleported = false;
}

void AElevatorShaftController::AbortTransition()
{
	// 인자 없는 콜백은 FadeTimeoutTimer에서만 사용한다. 직접 취소 경로는 별도 사유를 전달한다.
	AbortTransitionWithReason(TEXT("FadeTimeout"));
}

void AElevatorShaftController::AbortTransitionWithReason(const TCHAR* Reason)
{
	if (!HasAuthority() || !IsTransitionActive()) return;
	// 복귀·정리 전에 기록해야 실패 당시의 좌표와 완료 응답을 보존할 수 있다.
	UE_LOG(LogElevatorShaft, Warning, TEXT("[ElevatorAbort] Shaft=%s Execution=%s Reason=%s Timeout=%.2f PairTeleported=%d ShooterArrivalAck=%d PartnerArrivalAck=%d ShooterFadeAck=%d PartnerFadeAck=%d PairReady=%d GameOver=%d GenerationMatch=%d"),
		*GetName(), *Motion.ExecutionId.ToString(), Reason, TransitionTimeout, bPairTeleported,
		bShooterArrivalReady, bPartnerArrivalReady, bShooterFadeReady, bPartnerFadeReady,
		GameMode.IsValid() && GameMode->IsPairGameplayReady(), GameMode.IsValid() && GameMode->IsGameOverSelectionActive(),
		!Arena.IsValid() || Arena->GetGameplayGeneration() == Motion.Generation);
	UE_LOG(LogElevatorShaft, Warning, TEXT("[ElevatorAbortPawn] Role=Shooter %s Location=%s Controller=%s CurrentPawn=%s"),
		*DescribeArrivalReadiness(Motion.ExecutionId, BlockedShooter.Get()),
		BlockedShooter.IsValid() ? *BlockedShooter->GetActorLocation().ToString() : TEXT("None"),
		*GetNameSafe(ShooterController.Get()), *GetNameSafe(ShooterController.IsValid() ? ShooterController->GetPawn() : nullptr));
	UE_LOG(LogElevatorShaft, Warning, TEXT("[ElevatorAbortPawn] Role=Partner %s Location=%s Controller=%s CurrentPawn=%s"),
		*DescribeArrivalReadiness(Motion.ExecutionId, BlockedPartner.Get()),
		BlockedPartner.IsValid() ? *BlockedPartner->GetActorLocation().ToString() : TEXT("None"),
		*GetNameSafe(PartnerController.Get()), *GetNameSafe(PartnerController.IsValid() ? PartnerController->GetPawn() : nullptr));
	GetWorldTimerManager().ClearTimer(FadeTimeoutTimer);
	GetWorldTimerManager().ClearTimer(FadeMinimumTimer);
	GetWorldTimerManager().ClearTimer(MotionPhaseTimer);
	SetControllerAtArrival(false);
	RollbackPair();
	ReleaseTransition();
	StopMotionAudio();
	Motion.ExecutionId = FGuid::NewGuid();
	Motion.bArrival = false;
	Motion.bLanded = false;
	Motion.ArrivalStartTime = 0;
	if (ArrivalSection) ArrivalSection->SetActorTransform(Motion.ArrivalTransform);
	if (ArrivalDoor) ArrivalDoor->SetPresentationOnly(false);
	SetState(EElevatorShaftState::TransitionOut);
	RetryExit();
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
	DOREPLIFETIME(AElevatorShaftController, ArrivalSection);
	DOREPLIFETIME(AElevatorShaftController, ArrivalDoor);
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
	bool bInvalidTransport = false;
	TSet<const AExplosiveProp*> Seen;
	for (const AExplosiveProp* Prop : TransportExplosiveProps)
	{
		if (!IsTransportPropValid(Prop) || Seen.Contains(Prop))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("TransportExplosiveProps contains an invalid, attached or duplicate prop: %s."), *GetNameSafe(Prop))));
			bInvalidTransport = true;
			continue;
		}
		Seen.Add(Prop);
		// 배치 오류만 에디터에서 찾는다. 런타임은 통의 등록 소유권으로 중복을 차단한다.
		for (TActorIterator<AElevatorShaftController> It(GetWorld()); It; ++It)
		{
			if (*It != this && It->TransportExplosiveProps.Contains(Prop))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Transport prop %s is also registered to %s."), *Prop->GetName(), *It->GetName())));
				bInvalidTransport = true;
				break;
			}
		}
	}
	FString Error;
	if (!ValidateConfiguration(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return bInvalidTransport || Parent == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
