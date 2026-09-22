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

	ApplyDoorState(bIsOpen);

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
				SetDoorOpenInternal(true, false, false);
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
}

void AInteractableDoor::SetDoorOpen(bool bOpen)
{
	SetDoorOpenInternal(bOpen, true, true);
}

void AInteractableDoor::SetDoorOpenInternal(bool bOpen, bool bRecordProgress, bool bPlayAudio)
{
	if (bIsOpen == bOpen)
	{
		return;
	}

	//UE_LOG(LogTemp, Error, TEXT("Opened"));
	bIsOpen = bOpen;
	Multicast_SetDoorState(bIsOpen);
	ForceNetUpdate();

	if (HasAuthority() && bRecordProgress && bProgressIdRegistered)
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

	if (DoorCurve && bPlayAudio)
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
