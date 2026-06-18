#include "Interaction/InteractableDoor.h"
#include "Interaction/InteractableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Curves/CurveFloat.h"

AInteractableDoor::AInteractableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));

	DoorMeshLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshLeft"));
	DoorMeshRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshRight"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));
	DoorMeshLeft->SetupAttachment(RootComponent);
	DoorMeshRight->SetupAttachment(RootComponent);
}

void AInteractableDoor::BeginPlay()
{
	Super::BeginPlay();

	if (DoorCurve)
	{
		FOnTimelineFloat UpdateDelegate;
		UpdateDelegate.BindUFunction(this, FName("OnDoorTimelineUpdate"));
		DoorTimeline.AddInterpFloat(DoorCurve, UpdateDelegate);
	}
}

void AInteractableDoor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	DoorTimeline.TickTimeline(DeltaTime);
}

void AInteractableDoor::OnDoorTimelineUpdate(float Alpha)
{
	DoorMeshLeft->SetRelativeLocation(FMath::Lerp(FVector::ZeroVector, OpenOffsetLeft, Alpha));
	DoorMeshRight->SetRelativeLocation(FMath::Lerp(FVector::ZeroVector, OpenOffsetRight, Alpha));
}

UInteractableComponent* AInteractableDoor::GetInteractableComponent() const
{
	return InteractableComponent;
}

void AInteractableDoor::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !InteractableComponent)
	{
		return;
	}

	const FGameplayTagContainer InteractorTags = Interactor->GetOwnedGameplayTagsForQuery();
	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Door] Interact blocked by tags"));
		return;
	}

	bIsOpen = !bIsOpen;
	UE_LOG(LogTemp, Error, TEXT("DoorInteract"),bIsOpen);

	Multicast_SetDoorState(bIsOpen);
}

void AInteractableDoor::Multicast_SetDoorState_Implementation(bool bOpen)
{
	if (bOpen)
		DoorTimeline.Play();
	else
		DoorTimeline.Reverse();
}

void AInteractableDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractableDoor, bIsOpen);
}
