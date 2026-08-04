// Fill out your copyright notice in the Description page of Project Settings.
#include "PostProcess/TempDamagePannel.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Shooter/ShooterCharacter.h"

ATempDamagePannel::ATempDamagePannel()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PannelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PannelMesh"));
	PannelMesh->SetupAttachment(Root);
	PannelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PannelMesh->SetCollisionResponseToAllChannels(ECR_Block);

	DamageCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageCollision"));
	DamageCollision->SetupAttachment(Root);
	DamageCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageCollision->SetCollisionObjectType(ECC_WorldDynamic);
	DamageCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	DamageCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DamageCollision->SetGenerateOverlapEvents(true);

}

void ATempDamagePannel::BeginPlay()
{
	Super::BeginPlay();

	if (DamageCollision)
	{
		DamageCollision->OnComponentBeginOverlap.AddDynamic(
			this,
			&ATempDamagePannel::HandleDamageCollisionBeginOverlap
		);

		DamageCollision->OnComponentEndOverlap.AddDynamic(
			this,
			&ATempDamagePannel::HandleDamageCollisionEndOverlap
		);

	}
	
}

void ATempDamagePannel::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//UE_LOG(LogTemp, Error, TEXT("Ticking"));

	DamageAccumulatedTime += DeltaTime;

	if (IngCollision && Temp_Shooter)
	{
		const float SafeDamageInterval = FMath::Max(DamageInterval, KINDA_SMALL_NUMBER);
		if (DamageAccumulatedTime < SafeDamageInterval)
		{
			//UE_LOG(LogTemp, Error, TEXT("[Ticking] DamageAccumulatedTime < SafeDamageInterval"));

			return;
		}
		else
		{
			//UE_LOG(LogTemp, Error, TEXT("[Ticking] DamageAccumulatedTime >= SafeDamageInterval"));

			ApplyDamageToShooter(Temp_Shooter);
			DamageAccumulatedTime = 0.0f;
		}
	}
	else
	{
		//UE_LOG(LogTemp, Error, TEXT("[Ticking] !IngCollision && Temp_Shooter"));

	}
	
}

void ATempDamagePannel::OnCollision(AActor* CollidedActor)
{
	if (!HasAuthority())
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(CollidedActor);
	if (!Shooter)
	{
		return;
	}

	Temp_Shooter = Shooter;

	IngCollision = true;

	if (bApplyDamageImmediately && Temp_Shooter)
	{
		ApplyDamageToShooter(Temp_Shooter);
	}
}

void ATempDamagePannel::HandleDamageCollisionBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	OnCollision(OtherActor);
}

void ATempDamagePannel::HandleDamageCollisionEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(OtherActor);
	if (!Shooter || Shooter != Temp_Shooter)
	{
		return;
	}

	IngCollision = false;
	Temp_Shooter = nullptr;
	DamageAccumulatedTime = 0.0f;
}

void ATempDamagePannel::ApplyDamageToShooter(AShooterCharacter* Shooter)
{
	if (!Shooter || DamageAmount <= 0.0f)
	{
		return;
	}

	FDamageEvent DamageEvent;
	Shooter->TakeDamage(DamageAmount, DamageEvent, nullptr, this);
}
