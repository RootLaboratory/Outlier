// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierCheckpoint.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

// Sets default values
AOutlierCheckpoint::AOutlierCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(120.0f, 120.0f, 120.0f));
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	CheckpointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CheckpointMesh"));
	CheckpointMesh->SetupAttachment(Trigger);
	CheckpointMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CheckpointMesh->SetGenerateOverlapEvents(false);

	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Trigger);
}

// Called when the game starts or when spawned
void AOutlierCheckpoint::BeginPlay()
{
	Super::BeginPlay();	
}

FTransform AOutlierCheckpoint::GetSpawnTransform() const
{
	return SpawnPoint ? SpawnPoint->GetComponentTransform() : GetActorTransform();
}

void AOutlierCheckpoint::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	if (!HasAuthority())
	{
		return;
	}

	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(OtherActor);
	if (!ShooterCharacter)
	{
		return;
	}

	AOutlierGameMode* GM = GetWorld()->GetAuthGameMode<AOutlierGameMode>();
	if (GM)
	{
		GM->RegisterCheckpoint(ShooterCharacter, this);
	}
}
