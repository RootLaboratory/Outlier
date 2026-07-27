#include "Enemy/TestGeometryCollection.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestGeometryCollection, Log, All);

ATestGeometryCollection::ATestGeometryCollection()
{
	DeathGeometryCollectionComponent =
		CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("DeathGeometryCollectionComponent"));
	DeathGeometryCollectionComponent->SetupAttachment(GetMesh());
	DeathGeometryCollectionComponent->SetMobility(EComponentMobility::Movable);
	DeathGeometryCollectionComponent->SetAutoActivate(false);
	DeathGeometryCollectionComponent->SetVisibility(false);
	DeathGeometryCollectionComponent->SetHiddenInGame(true);
	DeathGeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeathGeometryCollectionComponent->SetSimulatePhysics(false);
	DeathGeometryCollectionComponent->SetEnableGravity(false);
}

void ATestGeometryCollection::BeginPlay()
{
	Super::BeginPlay();

	if (!DeathGeometryCollectionComponent)
	{
		return;
	}

	// Blueprint component defaults are applied after the native constructor and
	// can turn simulation back on. Keep the hidden collection at its authored
	// rest pose until death instead of allowing it to fall invisibly.
	DeathGeometryCollectionComponent->SetSimulatePhysics(false);
	DeathGeometryCollectionComponent->SetEnableGravity(false);
	DeathGeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeathGeometryCollectionComponent->ResetState();
	DeathGeometryCollectionComponent->Deactivate();
	DeathGeometryCollectionComponent->SetVisibility(false);
	DeathGeometryCollectionComponent->SetHiddenInGame(true);
}

void ATestGeometryCollection::HandleDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	const FVector DeathVelocity = GetVelocity();

	if (IsEnemyPossessed())
	{
		ClearPossessedPlayerState();
	}

	if (AController* CurrentController = GetController())
	{
		CurrentController->UnPossess();
	}

	SetReplicateMovement(false);
	MulticastActivateDeathGeometry(DeathVelocity);

	if (DeathProfile.DebrisLifetime > 0.0f)
	{
		SetLifeSpan(DeathProfile.DebrisLifetime);
	}
}

void ATestGeometryCollection::MulticastActivateDeathGeometry_Implementation(
	FVector_NetQuantize100 DeathVelocity)
{
	ActivateDeathGeometry(DeathVelocity);
}

void ATestGeometryCollection::ActivateDeathGeometry(const FVector& DeathVelocity)
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
		MovementComponent->DisableMovement();
	}

	if (StateTreeComponent)
	{
		StateTreeComponent->Deactivate();
	}

	if (UCapsuleComponent* CharacterCapsule = GetCapsuleComponent())
	{
		CharacterCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (USphereComponent* CoreHitbox = GetCoreHitboxComponent())
	{
		CoreHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (!DeathGeometryCollectionComponent)
	{
		return;
	}

	const UGeometryCollection* RestCollection =
		DeathGeometryCollectionComponent->GetRestCollection();
	if (!RestCollection || RestCollection->IsEmpty())
	{
		UE_LOG(
			LogTestGeometryCollection,
			Error,
			TEXT("[%s] Death Geometry Collection has no valid Rest Collection; keeping the source mesh visible."),
			*GetName());
		return;
	}

	USkeletalMeshComponent* CharacterMesh = GetMesh();
	const FVector SourceMeshBoundsOrigin =
		CharacterMesh
			? CharacterMesh->Bounds.Origin
			: GetActorLocation();

	DeathGeometryCollectionComponent->UpdateBounds();
	UE_LOG(
		LogTestGeometryCollection,
		Display,
		TEXT("[%s] GC before activation AttachedTo=%s ComponentLocation=%s SourceBounds=%s GCBounds=%s"),
		*GetName(),
		DeathGeometryCollectionComponent->GetAttachParent()
			? *DeathGeometryCollectionComponent->GetAttachParent()->GetName()
			: TEXT("None"),
		*DeathGeometryCollectionComponent->GetComponentLocation().ToCompactString(),
		*SourceMeshBoundsOrigin.ToCompactString(),
		*DeathGeometryCollectionComponent->Bounds.Origin.ToCompactString());

	// Cache and explicitly restore the authored world transform. This avoids the
	// collection inheriting a stale skeletal-mesh transform while its physics
	// proxy is being created.
	const FTransform DeathGeometryWorldTransform =
		DeathGeometryCollectionComponent->GetComponentTransform();
	DeathGeometryCollectionComponent->DetachFromComponent(
		FDetachmentTransformRules::KeepWorldTransform);
	DeathGeometryCollectionComponent->SetWorldTransform(
		DeathGeometryWorldTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	DeathGeometryCollectionComponent->SetVisibility(true);
	DeathGeometryCollectionComponent->SetHiddenInGame(false);
	DeathGeometryCollectionComponent->UpdateBounds();

	if (!DeathProfile.CollisionProfileName.IsNone())
	{
		DeathGeometryCollectionComponent->SetCollisionProfileName(
			DeathProfile.CollisionProfileName);
	}

	if (DeathProfile.bIgnorePawnCollision)
	{
		DeathGeometryCollectionComponent->SetCollisionResponseToChannel(
			ECC_Pawn,
			ECR_Ignore);
	}

	if (DeathProfile.bIgnoreCameraCollision)
	{
		DeathGeometryCollectionComponent->SetCollisionResponseToChannel(
			ECC_Camera,
			ECR_Ignore);
	}

	DeathGeometryCollectionComponent->SetGenerateOverlapEvents(false);
	DeathGeometryCollectionComponent->SetNotifyRigidBodyCollision(false);
	DeathGeometryCollectionComponent->SetNotifyBreaks(false);
	DeathGeometryCollectionComponent->SetNotifyGlobalCollision(false);
	DeathGeometryCollectionComponent->SetOneWayInteractionLevel(
		DeathProfile.bEnableOneWayInteraction
			? DeathProfile.OneWayInteractionLevel
			: INDEX_NONE);
	DeathGeometryCollectionComponent->SetCollisionEnabled(
		DeathProfile.bEnableSceneQueries
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::PhysicsOnly);
	DeathGeometryCollectionComponent->Activate(true);
	DeathGeometryCollectionComponent->SetSimulatePhysics(true);
	DeathGeometryCollectionComponent->SetEnableGravity(DeathProfile.bEnableGravity);

	if (!DeathGeometryCollectionComponent->IsSimulatingPhysics())
	{
		UE_LOG(
			LogTestGeometryCollection,
			Error,
			TEXT("[%s] Death Geometry Collection failed to start simulating; keeping the source mesh visible."),
			*GetName());
		DeathGeometryCollectionComponent->SetVisibility(false);
		DeathGeometryCollectionComponent->SetHiddenInGame(true);
		DeathGeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	if (CharacterMesh)
	{
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CharacterMesh->SetVisibility(false, false);
		CharacterMesh->SetHiddenInGame(true, false);
	}

	UE_LOG(
		LogTestGeometryCollection,
		Display,
		TEXT("[%s] Activated GC=%s Transform=%s BoundsOrigin=%s BoundsExtent=%s"),
		*GetName(),
		*RestCollection->GetPathName(),
		*DeathGeometryWorldTransform.ToHumanReadableString(),
		*DeathGeometryCollectionComponent->Bounds.Origin.ToCompactString(),
		*DeathGeometryCollectionComponent->Bounds.BoxExtent.ToCompactString());

	// Give Chaos one frame to finish creating its proxy before breaking the
	// active root cluster.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<ATestGeometryCollection> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(
			[WeakThis, DeathVelocity]()
			{
				if (ATestGeometryCollection* StrongThis = WeakThis.Get())
				{
					StrongThis->ReleaseDeathGeometry(DeathVelocity);
				}
			});
	}
}

void ATestGeometryCollection::ReleaseDeathGeometry(const FVector& DeathVelocity)
{
	if (!DeathGeometryCollectionComponent
		|| !DeathGeometryCollectionComponent->IsActive()
		|| !DeathGeometryCollectionComponent->IsSimulatingPhysics())
	{
		return;
	}

	if (DeathProfile.bCrumbleRootOnDeath)
	{
		DeathGeometryCollectionComponent->CrumbleActiveClusters();
	}

	// Give the released children another frame to become field targets before
	// applying the art-directed velocities.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<ATestGeometryCollection> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(
			[WeakThis, DeathVelocity]()
			{
				if (ATestGeometryCollection* StrongThis = WeakThis.Get())
				{
					StrongThis->ApplyDeathImpulses(DeathVelocity);
				}
			});
	}
}

void ATestGeometryCollection::ApplyDeathImpulses(const FVector& DeathVelocity)
{
	if (!DeathGeometryCollectionComponent
		|| !DeathGeometryCollectionComponent->IsActive()
		|| !DeathGeometryCollectionComponent->IsSimulatingPhysics())
	{
		return;
	}

	UE_LOG(
		LogTestGeometryCollection,
		Display,
		TEXT("[%s] Applying GC impulses Transform=%s BoundsOrigin=%s BoundsExtent=%s"),
		*GetName(),
		*DeathGeometryCollectionComponent->GetComponentTransform().ToHumanReadableString(),
		*DeathGeometryCollectionComponent->Bounds.Origin.ToCompactString(),
		*DeathGeometryCollectionComponent->Bounds.BoxExtent.ToCompactString());

	if (DeathProfile.InheritedVelocityScale > 0.0f && !DeathVelocity.IsNearlyZero())
	{
		DeathGeometryCollectionComponent->AddImpulse(
			DeathVelocity * DeathProfile.InheritedVelocityScale,
			NAME_None,
			true);
	}

	if (DeathProfile.RadialImpulseRadius > 0.0f
		&& DeathProfile.RadialImpulseStrength > 0.0f)
	{
		const FVector ExplosionCenter =
			DeathGeometryCollectionComponent->Bounds.Origin
			+ DeathGeometryCollectionComponent->GetComponentTransform().TransformVectorNoScale(
				DeathProfile.LocalExplosionCenterOffset);

		DeathGeometryCollectionComponent->AddRadialImpulse(
			ExplosionCenter,
			DeathProfile.RadialImpulseRadius,
			DeathProfile.RadialImpulseStrength,
			DeathProfile.RadialImpulseFalloff,
			DeathProfile.bIgnoreMass);
	}

	if (!DeathProfile.LocalDirectionalImpulse.IsNearlyZero())
	{
		const FVector WorldDirectionalImpulse =
			DeathGeometryCollectionComponent->GetComponentTransform().TransformVectorNoScale(
				DeathProfile.LocalDirectionalImpulse);

		DeathGeometryCollectionComponent->AddImpulse(
			WorldDirectionalImpulse,
			NAME_None,
			DeathProfile.bIgnoreMass);
	}

	if (!FMath::IsNearlyZero(DeathProfile.UpwardImpulseStrength))
	{
		DeathGeometryCollectionComponent->AddImpulse(
			FVector::UpVector * DeathProfile.UpwardImpulseStrength,
			NAME_None,
			DeathProfile.bIgnoreMass);
	}
}
