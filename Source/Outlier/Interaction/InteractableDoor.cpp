#include "Interaction/InteractableDoor.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "EngineUtils.h"

AInteractableDoor::AInteractableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Character 이동/물리 갱신 뒤의 몸체 위치를 보고, 문 이동보다 먼저 보호를 판정한다.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));

	DoorMeshLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshLeft"));
	DoorMeshRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshRight"));
	DoorMeshLeft->SetupAttachment(RootComponent);
	DoorMeshRight->SetupAttachment(RootComponent);

	SafetyRegionLeft = CreateDefaultSubobject<UBoxComponent>(TEXT("SafetyRegionLeft"));
	SafetyRegionRight = CreateDefaultSubobject<UBoxComponent>(TEXT("SafetyRegionRight"));
	for (UBoxComponent* Region : { SafetyRegionLeft.Get(), SafetyRegionRight.Get() })
	{
		Region->SetupAttachment(RootComponent);
		// Box는 범위 표시와 보호 조회에만 사용한다. 오버랩 갱신이나 물리 충돌은 만들지 않는다.
		Region->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Region->SetGenerateOverlapEvents(false);
		Region->SetBoxExtent(FVector::ZeroVector);
	}
}

void AInteractableDoor::BeginPlay()
{
	if (HasAuthority())
	{
		const AElevatorShaftSection* Section = nullptr;
		bool bEntranceDoor = false;
		// 부모 BeginPlay보다 먼저 문이 등록될 수 있으므로, 문이 직접 Controller 인스턴스의 ID를 읽는다.
		for (AActor* Parent = GetParentActor(); Parent; Parent = Parent->GetParentActor())
		{
			if (const AElevatorShaftSection* ParentSection = Cast<AElevatorShaftSection>(Parent)) Section = ParentSection;
			if (const AElevatorShaftController* Controller = Cast<AElevatorShaftController>(Parent))
			{
				DoorId = Controller->EntranceDoorId;
				bEntranceDoor = true;
				break;
			}
		}
		if (Section && !bEntranceDoor)
		{
			// 도착층은 Controller의 자식이 아니다. 초기 등록 시 한 번만 역참조해 BeginPlay 순서를 피한다.
			const AElevatorShaftController* ArrivalController = nullptr;
			bool bAmbiguous = false;
			for (TActorIterator<AElevatorShaftController> It(GetWorld()); It; ++It)
			{
				if (It->ArrivalSection != Section) continue;
				if (ArrivalController)
				{
					bAmbiguous = true;
					break;
				}
				ArrivalController = *It;
			}
			if (bAmbiguous)
			{
				// 잘못 연결된 두 Controller 중 하나의 저장 키를 임의로 선택하지 않는다.
				DoorId = NAME_None;
			}
			else if (ArrivalController && !ArrivalController->ArrivalDoorId.IsNone())
			{
				DoorId = ArrivalController->ArrivalDoorId;
			}
		}
	}
	Super::BeginPlay();

	ClosedLocationLeft = DoorMeshLeft ? DoorMeshLeft->GetRelativeLocation() : FVector::ZeroVector;
	ClosedLocationRight = DoorMeshRight ? DoorMeshRight->GetRelativeLocation() : FVector::ZeroVector;
	InitialLeftCollision = DoorMeshLeft ? DoorMeshLeft->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
	InitialRightCollision = DoorMeshRight ? DoorMeshRight->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
	InitializeSafetyRegion(SafetyRegionLeft, DoorMeshLeft, OpenOffsetLeft);
	InitializeSafetyRegion(SafetyRegionRight, DoorMeshRight, OpenOffsetRight);

	if (DoorCurve)
	{
		FOnTimelineFloat UpdateDelegate;
		UpdateDelegate.BindUFunction(this, FName("OnDoorTimelineUpdate"));
		DoorTimeline.AddInterpFloat(DoorCurve, UpdateDelegate);

		FOnTimelineEvent FinishedDelegate;
		FinishedDelegate.BindUFunction(this, FName("OnDoorTimelineFinished"));
		DoorTimeline.SetTimelineFinishedFunc(FinishedDelegate);
	}

	// 초기 열림은 배치 상태다. 여기서 Timeline을 돌리면 Gate가 이를 새 개방 완료로 오인할 수 있다.
	SnapDoorState(bInitiallyOpen);
	bDoorInitialized = true;
	if (!HasAuthority())
	{
		// 초기 복제가 BeginPlay보다 먼저 도착한 경우에도 서버의 이동 기준점을 적용한다.
		ApplyReplicatedDoorMotion(ReplicatedDoorMotion);
	}
	// RepNotify가 BeginPlay보다 먼저 실행돼도 BP에서 설정한 원래 충돌을 먼저 보관한다.
	if (bPresentationOnly)
	{
		ApplyPresentationOnly();
	}

	if (HasAuthority())
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
				EOutlierWorldProgressType::OpenedDoor,
				DoorId,
				this);
			if (bProgressIdRegistered
				&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::OpenedDoor, DoorId))
			{
				// 복원은 새 문 조작이 아니므로 진행 기록과 서버의 이동 사운드를 다시 발생시키지 않는다.
				SnapDoorState(true);
			}
		}
	}
}

void AInteractableDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::OpenedDoor,
				DoorId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AInteractableDoor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 닫힘 프레임은 반드시 검사 -> 방향 전환 또는 이동 순서로 처리한다.
	// 감지 프레임에는 Timeline을 진행하지 않아 현재 위치에서 그대로 다시 연다.
	if (TrySafetyReopen())
	{
		return;
	}
	DoorTimeline.TickTimeline(DeltaTime);
}

bool AInteractableDoor::TrySafetyReopen()
{
	if (!HasAuthority() || bPresentationOnly || bIsOpen || !DoorTimeline.IsPlaying() || !HasBlockingPlayer())
	{
		return false;
	}
	StartDoorMotion(true, true);
	return true;
}

void AInteractableDoor::OnDoorTimelineUpdate(float Alpha)
{
	if (DoorMeshLeft)
	{
		DoorMeshLeft->SetRelativeLocation(FMath::Lerp(ClosedLocationLeft, ClosedLocationLeft + OpenOffsetLeft, Alpha));
	}

	if (DoorMeshRight)
	{
		DoorMeshRight->SetRelativeLocation(FMath::Lerp(ClosedLocationRight, ClosedLocationRight + OpenOffsetRight, Alpha));
	}
}

void AInteractableDoor::OnDoorTimelineFinished()
{
	SetActorTickEnabled(false);
	if (HasAuthority())
	{
		PublishDoorMotion();
	}
	if (HasAuthority() && bMotionCompletionPending)
	{
		// 서버의 실제 문 이동만 알린다. 초기 배치/저장 복원에서는 Pending을 세우지 않는다.
		bMotionCompletionPending = false;
		OnDoorMotionFinished.Broadcast(this, bIsOpen);
	}
}

bool AInteractableDoor::HasMovementCurve() const
{
	if (!DoorCurve)
	{
		return false;
	}
	float StartTime = 0.0f;
	float EndTime = 0.0f;
	DoorCurve->GetTimeRange(StartTime, EndTime);
	return EndTime > StartTime;
}

bool AInteractableDoor::IsDoorMoving() const
{
	return bDoorInitialized ? DoorTimeline.IsPlaying() : ReplicatedDoorMotion.bMoving;
}

bool AInteractableDoor::SetPresentationOnly(bool bNewPresentationOnly)
{
	if (!HasAuthority() || !bDoorInitialized)
	{
		return false;
	}
	if (bPresentationOnly == bNewPresentationOnly)
	{
		return true;
	}
	// 닫힘 요청만 승인된 시점에는 충돌을 넘기지 않는다. 안전 반전 중에도 진입할 수 없다.
	if (bNewPresentationOnly && (bIsOpen || IsDoorMoving()))
	{
		return false;
	}
	bPresentationOnly = bNewPresentationOnly;
	ApplyPresentationOnly();
	ForceNetUpdate();
	return true;
}

void AInteractableDoor::ResetToInitialState()
{
	if (!HasAuthority() || !bDoorInitialized)
	{
		return;
	}
	// 재시도는 새 문 조작이 아니다. 충돌과 배치를 복원하되 완료/사운드/진행 기록은 만들지 않는다.
	SetPresentationOnly(false);
	SnapDoorState(bInitiallyOpen);
}

void AInteractableDoor::OnRep_PresentationOnly()
{
	if (bDoorInitialized)
	{
		ApplyPresentationOnly();
		if (!HasAuthority() && !bPresentationOnly)
		{
			// 재시도 이동 RPC가 외형 전용 해제보다 먼저 도착했으면 여기서 적용한다.
			ApplyReplicatedDoorMotion(ReplicatedDoorMotion);
		}
	}
}

void AInteractableDoor::ApplyPresentationOnly()
{
	if (DoorMeshLeft)
	{
		DoorMeshLeft->SetCollisionEnabled(bPresentationOnly ? ECollisionEnabled::NoCollision : InitialLeftCollision);
	}
	if (DoorMeshRight)
	{
		DoorMeshRight->SetCollisionEnabled(bPresentationOnly ? ECollisionEnabled::NoCollision : InitialRightCollision);
	}
	if (bPresentationOnly)
	{
		// 속성 복제 순서와 무관하게 이동용 문은 닫힌 외형을 유지한다.
		bIsOpen = false;
		bMotionCompletionPending = false;
		DoorTimeline.Stop();
		DoorTimeline.SetPlaybackPosition(0.0f, false, false);
		OnDoorTimelineUpdate(0.0f);
		SetActorTickEnabled(false);
	}
}

void AInteractableDoor::SnapDoorState(bool bOpen)
{
	bIsOpen = bOpen;
	bMotionCompletionPending = false;
	DoorTimeline.Stop();
	if (DoorCurve)
	{
		DoorTimeline.SetPlaybackPosition(bOpen ? DoorTimeline.GetTimelineLength() : 0.0f, false, false);
	}
	OnDoorTimelineUpdate(bOpen ? 1.0f : 0.0f);
	SetActorTickEnabled(false);
	if (HasAuthority())
	{
		PublishDoorMotion();
	}
}

void AInteractableDoor::SetDoorOpen(bool bOpen)
{
	TrySetDoorOpen(bOpen);
}

void AInteractableDoor::InitializeSafetyRegion(
	UBoxComponent* Region, UStaticMeshComponent* Mesh, const FVector& OpenOffset)
{
	if (!Region || !Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}

	// 문짝은 Root의 직접 자식이다. 닫힌/열린 메시 Bounds를 Root 공간에서 합쳐,
	// 열린 문에서도 닫힘 경로 전체를 검사한다. 감지 Box는 문짝을 따라 움직이지 않는다.
	const FBox ClosedBounds = Mesh->CalcBounds(Mesh->GetRelativeTransform()).GetBox();
	FBox PathBounds = ClosedBounds;
	PathBounds += FBox(ClosedBounds.Min + OpenOffset, ClosedBounds.Max + OpenOffset);
	PathBounds = PathBounds.ExpandBy(FMath::Max(0.0f, SafetyMargin));
	PathBounds.Max.Z += FMath::Max(0.0f, SafetyTopHeight);
	Region->SetRelativeLocation(PathBounds.GetCenter());
	Region->SetBoxExtent(PathBounds.GetExtent());
}

bool AInteractableDoor::HasBlockingPlayer() const
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || bPresentationOnly || !World)
	{
		return false;
	}

	// 닫기 요청과 실제 닫힘 이동 전에만 조회한다. 오버랩 캐시 대신 현재 충돌 상태를 읽어
	// 스폰/빙의/리스폰 직후에도 유효한 몸체와 현재 조종 상태로 판정한다.
	// 문이 소유한 버퍼를 재사용해 닫힘 프레임마다 결과 배열을 새로 할당하지 않는다.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DoorPlayerSafety), false, this);
	for (const UBoxComponent* Region : { SafetyRegionLeft.Get(), SafetyRegionRight.Get() })
	{
		if (!Region || Region->GetUnscaledBoxExtent().IsNearlyZero())
		{
			continue;
		}
		SafetyOverlaps.Reset();
		World->OverlapMultiByObjectType(SafetyOverlaps, Region->GetComponentLocation(),
			Region->GetComponentQuat(), FCollisionObjectQueryParams::AllObjects,
			FCollisionShape::MakeBox(Region->GetScaledBoxExtent()), QueryParams);
		for (const FOverlapResult& Overlap : SafetyOverlaps)
		{
			const ACharacter* Character = Cast<ACharacter>(Overlap.GetActor());
			if (!IsValid(Character) || Overlap.GetComponent() != Character->GetCapsuleComponent())
			{
				continue;
			}

			// UE 5.7의 IsPlayerControlled는 PlayerState를 기준으로 한다.
			// 보호 대상은 현재 조종 중인 몸체이므로 서버의 Controller -> Pawn 연결을 확인한다.
			const AController* Controller = Character->GetController();
			if (IsValid(Controller) && Controller->IsPlayerController() && Controller->GetPawn() == Character)
			{
				return true;
			}
		}
	}
	return false;
}

bool AInteractableDoor::TrySetDoorOpen(bool bOpen)
{
	if (!HasAuthority() || bPresentationOnly)
	{
		return false;
	}
	// 새 닫기의 상태/저장/사운드를 변경하기 전에 검사한다. 이미 진행 중인
	// 닫힘의 안전 취소는 새 요청의 승인 여부와 별도로 처리한다.
	if (!bOpen && HasBlockingPlayer())
	{
		// 같은 목표의 반복 요청도 보호 검사를 건너뛰지 않는다. 이미 닫히는 중이면
		// 새 요청은 거절하되, 진행 중인 이동은 별도로 안전 취소한다.
		if (!bIsOpen && DoorTimeline.IsPlaying())
		{
			StartDoorMotion(true, true);
		}
		return false;
	}
	if (bIsOpen == bOpen)
	{
		return true;
	}

	StartDoorMotion(bOpen, false);
	return true;
}

void AInteractableDoor::StartDoorMotion(bool bOpen, bool bSafetyReopen)
{
	bIsOpen = bOpen;
	// 안전 개방은 정상 진행용 완료를 만들지 않는다. 중단된 닫힘의 Pending도 여기서 취소한다.
	ApplyDoorState(bOpen);
	bMotionCompletionPending = !bSafetyReopen && DoorTimeline.IsPlaying();
	PublishDoorMotion();

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			if (!bOpen)
			{
				bOpenProgressBeforeClosing = SaveSubsystem->HasWorldProgress(
					EOutlierWorldProgressType::OpenedDoor, DoorId);
			}
			// 안전 개방은 진행 성공이 아닌 닫기 취소다. 닫기 전 기록으로 되돌려
			// Level1 복원 로직이 안전 개방을 완료된 입장 연출로 해석하지 않게 한다.
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::OpenedDoor,
				DoorId,
				bSafetyReopen ? bOpenProgressBeforeClosing : bOpen);
		}
	}

	if (DoorCurve)
	{
		PlayDoorMovementAudio(bIsOpen);
	}
	if (bSafetyReopen)
	{
		OnDoorSafetyReopenStarted.Broadcast(this);
	}
}

void AInteractableDoor::ToggleDoor()
{
	SetDoorOpen(!bIsOpen);
}

bool AInteractableDoor::PlayDoorMovementAudio(bool bOpen)
{
	if (!HasAuthority())
	{
		return false;
	}

	const FGameplayTag MovementContextTag = FGameplayTag::RequestGameplayTag(
		bOpen
			? TEXT("Audio.Context.Object.Door.Open")
			: TEXT("Audio.Context.Object.Door.Close"));

	return UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		MovementContextTag);
}

void AInteractableDoor::PublishDoorMotion()
{
	ReplicatedDoorMotion.bOpen = bIsOpen;
	ReplicatedDoorMotion.bMoving = DoorTimeline.IsPlaying();
	ReplicatedDoorMotion.PlaybackPosition = DoorTimeline.GetPlaybackPosition();
	ReplicatedDoorMotion.ServerTimeSeconds = GetWorld()->GetTimeSeconds();
	++ReplicatedDoorMotion.Revision;
	Multicast_SetDoorMotion(ReplicatedDoorMotion);
	ForceNetUpdate();
}

void AInteractableDoor::Multicast_SetDoorMotion_Implementation(const FDoorMotionState& State)
{
	if (!HasAuthority())
	{
		if (State.Revision > ReplicatedDoorMotion.Revision)
		{
			ReplicatedDoorMotion = State;
		}
		ApplyReplicatedDoorMotion(State);
	}
}

void AInteractableDoor::OnRep_DoorMotion()
{
	ApplyReplicatedDoorMotion(ReplicatedDoorMotion);
}

void AInteractableDoor::ApplyReplicatedDoorMotion(const FDoorMotionState& IncomingState)
{
	if (!bDoorInitialized)
	{
		// 초기화 전 최신 RPC 뒤에 오래된 속성 복제가 올 수도 있다. BeginPlay까지
		// 가장 최신 기준점 한 건만 보관해 초기 배치가 안전 반전을 되돌리지 않게 한다.
		if (IncomingState.Revision > PendingInitialDoorMotion.Revision)
		{
			PendingInitialDoorMotion = IncomingState;
		}
		return;
	}
	const FDoorMotionState State = PendingInitialDoorMotion.Revision > IncomingState.Revision
		? PendingInitialDoorMotion : IncomingState;
	PendingInitialDoorMotion = FDoorMotionState();
	if (State.Revision <= LastAppliedMotionRevision)
	{
		return;
	}
	// RPC와 속성 복제가 같은 전환을 전달하거나 오래된 전환이 뒤늦게 와도 재시작하지 않는다.
	if (bPresentationOnly)
	{
		// 해제 전에 도착한 재시도 기준점은 보류한다. 뒤늦은 속성 복제가 이를 덮지 않게 한다.
		PendingInitialDoorMotion = State;
		ApplyPresentationOnly();
		return;
	}
	LastAppliedMotionRevision = State.Revision;
	bIsOpen = State.bOpen;
	DoorTimeline.Stop();
	if (!State.bMoving || !HasMovementCurve())
	{
		DoorTimeline.SetPlaybackPosition(bIsOpen ? DoorTimeline.GetTimelineLength() : 0.0f, false, false);
		OnDoorTimelineUpdate(bIsOpen ? 1.0f : 0.0f);
		SetActorTickEnabled(false);
		return;
	}

	// 로컬 Timeline 위치에서 단순 반전하지 않는다. 서버의 전환 위치에 전송 지연을
	// 더해 복제 갱신이 합쳐지거나 이동 중 새로 접속한 클라이언트도 같은 방향/위치로 맞춘다.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Elapsed = GameState
		? FMath::Max(0.0, GameState->GetServerWorldTimeSeconds() - State.ServerTimeSeconds) : 0.0;
	const float Position = FMath::Clamp(State.PlaybackPosition
		+ static_cast<float>(Elapsed) * (bIsOpen ? 1.0f : -1.0f), 0.0f, DoorTimeline.GetTimelineLength());
	DoorTimeline.SetPlaybackPosition(Position, false);
	ApplyDoorState(bIsOpen);
}

void AInteractableDoor::ApplyDoorState(bool bOpen)
{
	if (!HasMovementCurve())
	{
		OnDoorTimelineUpdate(bOpen ? 1.0f : 0.0f);
		DoorTimeline.Stop();
		SetActorTickEnabled(false);
		return;
	}

	const float TargetPosition = bOpen ? DoorTimeline.GetTimelineLength() : 0.0f;
	if (FMath::IsNearlyEqual(DoorTimeline.GetPlaybackPosition(), TargetPosition))
	{
		// 닫기 시작 직후 같은 열린 끝점에서 반전될 수도 있다. Tick만 끄면
		// Timeline의 이전 닫힘 방향이 남으므로 재생 상태까지 함께 정리한다.
		DoorTimeline.Stop();
		SetActorTickEnabled(false);
		return;
	}

	SetActorTickEnabled(true);

	if (bOpen)
	{
		DoorTimeline.Play();
	}
	else
	{
		DoorTimeline.Reverse();
	}
}

void AInteractableDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractableDoor, ReplicatedDoorMotion);
	DOREPLIFETIME(AInteractableDoor, bPresentationOnly);
}
