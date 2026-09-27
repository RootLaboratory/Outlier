#include "Interaction/InteractableDoor.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
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
}

void AInteractableDoor::BeginPlay()
{
	Super::BeginPlay();

	ClosedLocationLeft = DoorMeshLeft ? DoorMeshLeft->GetRelativeLocation() : FVector::ZeroVector;
	ClosedLocationRight = DoorMeshRight ? DoorMeshRight->GetRelativeLocation() : FVector::ZeroVector;

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
	if (!HasAuthority() || bIsOpen == bOpen)
	{
		return;
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
