// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerShieldSphere.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Shooter/ShooterCharacter.h"

APartnerShieldSphere::APartnerShieldSphere()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	ShieldCollision = CreateDefaultSubobject<USphereComponent>(TEXT("ShieldCollision"));
	SetRootComponent(ShieldCollision);
	ShieldCollision->InitSphereRadius(ShieldRadius);
	ApplyTraceOnlyCollision();

	ShieldVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldVisual"));
	ShieldVisual->SetupAttachment(ShieldCollision);
	ShieldVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldVisual->SetGenerateOverlapEvents(false);
	ShieldVisual->SetOwnerNoSee(true);
	ShieldVisual->SetOnlyOwnerSee(false);
}

void APartnerShieldSphere::BeginPlay()
{
	Super::BeginPlay();

	ShieldCollision->OnComponentHit.AddDynamic(this, &APartnerShieldSphere::HandleShieldHit);
	EnsureDynamicMaterial();
	ApplyShieldRadius();
	ApplyTargetTransform();
	RefreshOwnerVisibility();
}

void APartnerShieldSphere::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyTraceOnlyCollision();
	ApplyShieldRadius();
	EnsureDynamicMaterial();
}

void APartnerShieldSphere::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ApplyTargetTransform();
}

void APartnerShieldSphere::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APartnerShieldSphere, ShieldTarget);
	DOREPLIFETIME(APartnerShieldSphere, SourcePartner);
	DOREPLIFETIME(APartnerShieldSphere, ShieldRadius);
	DOREPLIFETIME(APartnerShieldSphere, TargetRelativeLocation);
}

void APartnerShieldSphere::InitializeShield(AShooterCharacter* InShieldTarget, APartnerCharacter* InSourcePartner)
{
	if (!HasAuthority())
	{
		return;
	}

	ShieldTarget = InShieldTarget;
	SourcePartner = InSourcePartner;

	SetOwner(ShieldTarget);
	ApplyShieldRadius();
	ApplyTargetTransform();
	RefreshOwnerVisibility();
	ForceNetUpdate();
}

void APartnerShieldSphere::SetShieldMaterial(UMaterialInterface* InMaterial)
{
	ShieldMaterial = InMaterial;
	ShieldMID = nullptr;
	EnsureDynamicMaterial();
}

void APartnerShieldSphere::SetShieldRadius(float InShieldRadius)
{
	ShieldRadius = FMath::Max(InShieldRadius, 1.0f);
	ApplyShieldRadius();

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void APartnerShieldSphere::SetTargetRelativeLocation(FVector InTargetRelativeLocation)
{
	TargetRelativeLocation = InTargetRelativeLocation;
	ApplyTargetTransform();

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void APartnerShieldSphere::ApplyShieldDamage(float DamageAmount)
{
	if (!HasAuthority() || !ShieldTarget || DamageAmount <= 0.0f)
	{
		return;
	}

	ShieldTarget->ApplyDamageInternal(DamageAmount);
}

void APartnerShieldSphere::EndShield()
{
	if (HasAuthority())
	{
		Destroy();
	}
}

void APartnerShieldSphere::OnRep_ShieldTarget()
{
	SetOwner(ShieldTarget);
	ApplyTargetTransform();
	RefreshOwnerVisibility();
}

void APartnerShieldSphere::OnRep_ShieldRadius()
{
	ApplyShieldRadius();
}

void APartnerShieldSphere::HandleShieldHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	/*if (!HasAuthority() || !OtherActor || OtherActor == ShieldTarget || OtherActor == SourcePartner)
	{
		UE_LOG(LogTemp, Error, TEXT("HandleShieldHit"));

		return;
	}*/
}

void APartnerShieldSphere::ApplyTargetTransform()
{
	if (!ShieldTarget)
	{
		return;
	}

	const FVector TargetLocation = ShieldTarget->GetActorTransform().TransformPosition(TargetRelativeLocation);
	SetActorLocation(TargetLocation);
}

void APartnerShieldSphere::ApplyShieldRadius()
{
	if (ShieldCollision)
	{
		ShieldCollision->SetSphereRadius(ShieldRadius, true);
	}

	if (ShieldVisual)
	{
		ShieldVisual->SetRelativeLocation(FVector::ZeroVector);

		const UStaticMesh* Mesh = ShieldVisual->GetStaticMesh();
		if (!Mesh)
		{
			return;
		}

		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
		const float MeshRadius = MeshBounds.SphereRadius;

		if (MeshRadius > KINDA_SMALL_NUMBER)
		{
			const float Scale = ShieldRadius / MeshRadius;
			ShieldVisual->SetRelativeScale3D(FVector(Scale));
		}
	}
}

void APartnerShieldSphere::ApplyTraceOnlyCollision()
{
	if (!ShieldCollision)
	{
		return;
	}

	ShieldCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ShieldCollision->SetCollisionObjectType(ECC_PhysicsBody);
	ShieldCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	ShieldCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	ShieldCollision->SetGenerateOverlapEvents(false);
	ShieldCollision->SetNotifyRigidBodyCollision(false);
}

void APartnerShieldSphere::RefreshOwnerVisibility()
{
	if (ShieldVisual)
	{
	  ShieldVisual->SetOwnerNoSee(true);
	  ShieldVisual->SetOnlyOwnerSee(false);
	}
}

void APartnerShieldSphere::EnsureDynamicMaterial()
{
	if (!ShieldVisual || ShieldMID)
	{
		return;
	}

	if (ShieldMaterial)
	{
		ShieldMID = UMaterialInstanceDynamic::Create(ShieldMaterial, this);
		ShieldVisual->SetMaterial(0, ShieldMID);
	}
	else if (UMaterialInterface* CurrentMaterial = ShieldVisual->GetMaterial(0))
	{
		ShieldMID = ShieldVisual->CreateAndSetMaterialInstanceDynamicFromMaterial(0, CurrentMaterial);
	}
}

