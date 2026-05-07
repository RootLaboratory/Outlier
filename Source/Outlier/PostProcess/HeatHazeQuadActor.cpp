// Fill out your copyright notice in the Description page of Project Settings.


#include "HeatHazeQuadActor.h"
#include "HeatHazeSourceComponent.h"

// Sets default values
AHeatHazeQuadActor::AHeatHazeQuadActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	QuadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("QuadMesh"));
	QuadMesh->SetupAttachment(Root);

	HeatHazeSource = CreateDefaultSubobject<UHeatHazeSourceComponent>(TEXT("HeatHazeSource"));
	HeatHazeSource->SetupAttachment(Root);

	HeatHazeSource->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
	//SetLifeSpan(1.5f);
}

// Called when the game starts or when spawned
void AHeatHazeQuadActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AHeatHazeQuadActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

