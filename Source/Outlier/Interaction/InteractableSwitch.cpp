#include "Interaction/InteractableSwitch.h"

#include "Components/StaticMeshComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractableDoor.h"

AInteractableSwitch::AInteractableSwitch()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SwitchRoot"));

	SwitchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwitchMesh"));
	SwitchMesh->SetupAttachment(RootComponent);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
}

UInteractableComponent* AInteractableSwitch::GetInteractableComponent() const
{
	return InteractableComponent;
}

void AInteractableSwitch::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Interactor || !InteractableComponent)
	{
		return;
	}

	const FGameplayTagContainer InteractorTags = Interactor->GetOwnedGameplayTagsForQuery();
	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] Interact blocked by tags"));
		return;
	}

	if (!TargetDoor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Switch] TargetDoor is null Actor=%s"), *GetName());
		return;
	}

	if (bCanToggleDoor)
	{
		TargetDoor->ToggleDoor();
	}
	else
	{
		TargetDoor->SetDoorOpen(true);
	}

	Multicast_OnSwitchActivated(Interactor);
}

void AInteractableSwitch::Multicast_OnSwitchActivated_Implementation(AFirstPersonCharacter* Interactor)
{
	OnSwitchActivated(Interactor);
}
