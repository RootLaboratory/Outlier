// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/TagbaseedInteractTestActor.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"

// Sets default values
ATagbaseedInteractTestActor::ATagbaseedInteractTestActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	InteractableComponent =
		CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));

}

// Called when the game starts or when spawned
void ATagbaseedInteractTestActor::BeginPlay()
{
	Super::BeginPlay();
	
}

UInteractableComponent* ATagbaseedInteractTestActor::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool ATagbaseedInteractTestActor::Interact(AFirstPersonCharacter* Interactor)
{

	if (!Interactor || !InteractableComponent)
	{
		return false;
	}

	const FGameplayTagContainer InteractorTags =
		Interactor->GetOwnedGameplayTagsForQuery();

	if (!InteractableComponent->CanInteract(InteractorTags))
	{
		UE_LOG(LogTemp, Warning, TEXT("Interact blocked by tags"));
		return false;
	}

	//UE_LOG(LogTemp, Warning, TEXT("Interact valid"));
	return true;
}


