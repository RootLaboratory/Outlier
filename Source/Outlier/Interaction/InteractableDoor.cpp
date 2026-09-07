#include "Interaction/InteractableDoor.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

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
	if (bIsOpen == bOpen)
	{
		return;
	}

	bIsOpen = bOpen;
	Multicast_SetDoorState(bIsOpen);

	if (DoorCurve)
	{
		PlayDoorMovementAudio();
	}
}

void AInteractableDoor::ToggleDoor()
{
	SetDoorOpen(!bIsOpen);
}

bool AInteractableDoor::PlayDoorMovementAudio()
{
	// A door normally has no owning connection, so replicated playback must
	// originate on the authority instead of trying a client Server RPC.
	if (!HasAuthority())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][DoorRequestSkipped] Door='%s' Reason=NoAuthority"),
			*GetName());
		return false;
	}

	if (!DoorMovementAudioEventTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][DoorRequestSkipped] Door='%s' Reason=NoEventTag"),
			*GetName());
		return false;
	}

	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!AudioSubsystem)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][DoorRequestSkipped] Door='%s' Event='%s' Reason=NoSubsystem"),
			*GetName(),
			*DoorMovementAudioEventTag.ToString());
		return false;
	}

	FOutlierAudioPlayRequest Request;
	Request.EventTag = DoorMovementAudioEventTag;
	Request.ContextTags = DoorMovementAudioContextTags;
	Request.EmitterActor = this;
	Request.Location = GetActorLocation();
	Request.bHasLocation = true;

	UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][DoorRequest] Door='%s' Event='%s' Location=%s Context='%s'"),
		*GetName(),
		*Request.EventTag.ToString(),
		*Request.Location.ToCompactString(),
		*Request.ContextTags.ToStringSimple());

	const bool bAccepted = AudioSubsystem->PlayRelevantAtLocationFromServer(Request);
	UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][DoorRequestResult] Door='%s' Event='%s' Accepted=%d"),
		*GetName(),
		*Request.EventTag.ToString(),
		bAccepted);
	return bAccepted;
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
