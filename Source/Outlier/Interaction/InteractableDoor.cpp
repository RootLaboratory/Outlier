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
#include "Net/UnrealNetwork.h"
#include "Outlier.h"
#include "Save/OutlierSaveSubSystem.h"

AInteractableDoor::AInteractableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
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
		// Box는 범위 표시와 요청 검사에만 사용한다. 이동 중 오버랩 갱신이나 물리 충돌은 만들지 않는다.
		Region->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Region->SetGenerateOverlapEvents(false);
		Region->SetBoxExtent(FVector::ZeroVector);
	}
}

void AInteractableDoor::BeginPlay()
{
	Super::BeginPlay();

	ClosedLocationLeft = DoorMeshLeft ? DoorMeshLeft->GetRelativeLocation() : FVector::ZeroVector;
	ClosedLocationRight = DoorMeshRight ? DoorMeshRight->GetRelativeLocation() : FVector::ZeroVector;
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
	DoorTimeline.TickTimeline(DeltaTime);
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
		ForceNetUpdate();
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
	if (!HasAuthority() || !World)
	{
		return false;
	}

	// 닫기 요청 시 두 범위만 조회한다. 오버랩 캐시 대신 현재 충돌 상태를 읽어
	// 스폰/빙의/리스폰 직후에도 유효한 몸체와 현재 조종 상태로 판정한다.
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DoorPlayerSafety), false, this);
	for (const UBoxComponent* Region : { SafetyRegionLeft.Get(), SafetyRegionRight.Get() })
	{
		if (!Region || Region->GetUnscaledBoxExtent().IsNearlyZero())
		{
			continue;
		}
		Overlaps.Reset();
		World->OverlapMultiByObjectType(Overlaps, Region->GetComponentLocation(),
			Region->GetComponentQuat(), FCollisionObjectQueryParams::AllObjects,
			FCollisionShape::MakeBox(Region->GetScaledBoxExtent()), QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
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
	if (!HasAuthority())
	{
		return false;
	}
	if (bIsOpen == bOpen)
	{
		return true;
	}

	// 보호 검사는 모든 부수 효과보다 먼저 실행한다. 거절된 요청은 저장 상태,
	// Timeline, 완료 통지, 문 사운드를 변경하지 않고 호출자에게 실패를 돌려준다.
	if (!bOpen && HasBlockingPlayer())
	{
		return false;
	}

	bIsOpen = bOpen;
	bMotionCompletionPending = HasMovementCurve();
	Multicast_SetDoorState(bIsOpen);
	ForceNetUpdate();

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::OpenedDoor,
				DoorId,
				bIsOpen);
		}
	}

	if (DoorCurve)
	{
		PlayDoorMovementAudio(bIsOpen);
	}
	return true;
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

void AInteractableDoor::Multicast_SetDoorState_Implementation(bool bOpen)
{
	ApplyDoorState(bOpen);
}

void AInteractableDoor::OnRep_IsOpen()
{
	ApplyDoorState(bIsOpen);
}

void AInteractableDoor::ApplyDoorState(bool bOpen)
{
	if (!DoorCurve)
	{
		SetActorTickEnabled(false);
		return;
	}

	const float TargetPosition = bOpen ? DoorTimeline.GetTimelineLength() : 0.0f;
	if (FMath::IsNearlyEqual(DoorTimeline.GetPlaybackPosition(), TargetPosition))
	{
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
	DOREPLIFETIME(AInteractableDoor, bIsOpen);
}
