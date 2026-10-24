// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerShieldSphere.h"
#include "Audio/OutlierAudioFeedback.h"

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
	if (HasAuthority()) BaseShieldScale = GetActorScale3D();

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
	DOREPLIFETIME(APartnerShieldSphere, ShieldTargetActor);
	DOREPLIFETIME(APartnerShieldSphere, SourcePartner);
	DOREPLIFETIME(APartnerShieldSphere, ShieldRadius);
	DOREPLIFETIME(APartnerShieldSphere, TargetRelativeLocation);
	DOREPLIFETIME(APartnerShieldSphere, RecipientLocalTransform);
	DOREPLIFETIME(APartnerShieldSphere, BaseShieldScale);
}

float APartnerShieldSphere::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	if (!HasAuthority() || !IsValid(GetShieldTargetActor()) || Request.DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float Applied = OutlierDamage::Apply(GetShieldTargetActor(), Request);
	OutlierAudioFeedback::World(this, TEXT("Audio.Context.Object.Shield.Deflect"));
	return Applied;
}

void APartnerShieldSphere::InitializeShield(AShooterCharacter* InShieldTarget, APartnerCharacter* InSourcePartner)
{
	InitializeShieldForActor(InShieldTarget, InSourcePartner);
}

AActor* APartnerShieldSphere::GetShieldTargetActor() const
{
	return ShieldTargetActor ? ShieldTargetActor.Get() : ShieldTarget.Get();
}

void APartnerShieldSphere::InitializeShieldForActor(AActor* InShieldTarget, APartnerCharacter* InSourcePartner)
{
	if (!HasAuthority())
	{
		return;
	}

	ShieldTargetActor = InShieldTarget;
	ShieldTarget = Cast<AShooterCharacter>(InShieldTarget);
	SourcePartner = InSourcePartner;

	SetOwner(GetShieldTargetActor());
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

void APartnerShieldSphere::SetRecipientLocalTransform(const FTransform& InLocalTransform)
{
	if (!HasAuthority()) return;
	RecipientLocalTransform = InLocalTransform;
	ApplyTargetTransform();
	ForceNetUpdate();
}

void APartnerShieldSphere::OnRep_RecipientLocalTransform()
{
	ApplyTargetTransform();
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
	SetOwner(GetShieldTargetActor());
	ApplyTargetTransform();
	RefreshOwnerVisibility();
}

void APartnerShieldSphere::OnRep_ShieldRadius()
{
	ApplyShieldRadius();
}

void APartnerShieldSphere::ApplyTargetTransform()
{
	AActor* Target = GetShieldTargetActor();
	if (!IsValid(Target))
	{
		return;
	}

	const FVector TargetLocation = Target->GetActorTransform().TransformPosition(TargetRelativeLocation);
	const FTransform BaseTransform(Target->GetActorQuat(), TargetLocation, BaseShieldScale);
	// UE composes local transforms left-to-right: recipient adjustment, then shield placement.
	SetActorTransform(RecipientLocalTransform * BaseTransform);
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
