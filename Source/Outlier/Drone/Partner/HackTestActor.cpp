// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/HackTestActor.h"
#include "Drone/Partner/HackableComponent.h"

// Sets default values
AHackTestActor::AHackTestActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	HackComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackComponent"));
}

void AHackTestActor::BeginPlay()
{
	Super::BeginPlay();
	
}

UHackableComponent* AHackTestActor::GetHackableComponent_Implementation() const
{
	return HackComponent;
}





