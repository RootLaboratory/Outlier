#include "Enemy/Death/OutlierDeathDebrisActor.h"

#include "Engine/World.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Outlier.h"
#include "TimerManager.h"

AOutlierDeathDebrisActor::AOutlierDeathDebrisActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 연출 전용이다. 복제하지 않고, 서버 권위 판정에도 관여하지 않는다.
	bReplicates = false;
	SetReplicateMovement(false);

	DebrisComponent = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("DebrisComponent"));
	SetRootComponent(DebrisComponent);
	DebrisComponent->SetMobility(EComponentMobility::Movable);
	DebrisComponent->SetAutoActivate(false);
	DebrisComponent->SetSimulatePhysics(false);
	DebrisComponent->SetEnableGravity(false);
	DebrisComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

AOutlierDeathDebrisActor* AOutlierDeathDebrisActor::SpawnLocalDebris(
	UWorld* World,
	const UGeometryCollection* RestCollection,
	const FTransform& SpawnTransform,
	const FVector& InheritedVelocity,
	const FGeometryCollectionDeathProfile& Profile)
{
	if (!World || !RestCollection)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	AOutlierDeathDebrisActor* Debris = World->SpawnActor<AOutlierDeathDebrisActor>(
		AOutlierDeathDebrisActor::StaticClass(),
		SpawnTransform,
		SpawnParameters);
	if (!Debris)
	{
		return nullptr;
	}

	if (!Debris->BeginDebris(RestCollection, InheritedVelocity, Profile))
	{
		Debris->Destroy();
		return nullptr;
	}

	if (Profile.DebrisLifetime > 0.0f)
	{
		Debris->SetLifeSpan(Profile.DebrisLifetime);
	}

	return Debris;
}

bool AOutlierDeathDebrisActor::BeginDebris(
	const UGeometryCollection* RestCollection,
	const FVector& InheritedVelocity,
	const FGeometryCollectionDeathProfile& Profile)
{
	if (!DebrisComponent || !RestCollection || RestCollection->IsEmpty())
	{
		return false;
	}

	DebrisProfile = Profile;

	DebrisComponent->SetRestCollection(RestCollection);

	if (!DebrisProfile.CollisionProfileName.IsNone())
	{
		DebrisComponent->SetCollisionProfileName(DebrisProfile.CollisionProfileName);
	}

	if (DebrisProfile.bIgnorePawnCollision)
	{
		DebrisComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	if (DebrisProfile.bIgnoreCameraCollision)
	{
		DebrisComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	DebrisComponent->SetGenerateOverlapEvents(false);
	DebrisComponent->SetNotifyRigidBodyCollision(false);
	DebrisComponent->SetNotifyBreaks(false);
	DebrisComponent->SetNotifyGlobalCollision(false);
	DebrisComponent->SetOneWayInteractionLevel(
		DebrisProfile.bEnableOneWayInteraction
			? DebrisProfile.OneWayInteractionLevel
			: INDEX_NONE);
	DebrisComponent->SetCollisionEnabled(
		DebrisProfile.bEnableSceneQueries
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::PhysicsOnly);

	DebrisComponent->Activate(true);
	DebrisComponent->SetSimulatePhysics(true);
	DebrisComponent->SetEnableGravity(DebrisProfile.bEnableGravity);
	DebrisComponent->UpdateBounds();

	if (!DebrisComponent->IsSimulatingPhysics())
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[DeathDebris] '%s' failed to start simulating; debris discarded."),
			*RestCollection->GetPathName());
		return false;
	}

	// Chaos 가 물리 프록시를 다 만든 다음 프레임에 클러스터를 분해한다.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<AOutlierDeathDebrisActor> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(
			[WeakThis, InheritedVelocity]()
			{
				if (AOutlierDeathDebrisActor* StrongThis = WeakThis.Get())
				{
					StrongThis->ReleaseClusters(InheritedVelocity);
				}
			});
	}

	return true;
}

void AOutlierDeathDebrisActor::ReleaseClusters(const FVector& InheritedVelocity)
{
	if (!DebrisComponent
		|| !DebrisComponent->IsActive()
		|| !DebrisComponent->IsSimulatingPhysics())
	{
		return;
	}

	if (DebrisProfile.bCrumbleRootOnDeath)
	{
		DebrisComponent->CrumbleActiveClusters();
	}

	// 분해된 조각들이 필드 대상이 되는 데 한 프레임이 더 필요하다.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<AOutlierDeathDebrisActor> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(
			[WeakThis, InheritedVelocity]()
			{
				if (AOutlierDeathDebrisActor* StrongThis = WeakThis.Get())
				{
					StrongThis->ApplyImpulses(InheritedVelocity);
				}
			});
	}
}

void AOutlierDeathDebrisActor::ApplyImpulses(const FVector& InheritedVelocity)
{
	if (!DebrisComponent
		|| !DebrisComponent->IsActive()
		|| !DebrisComponent->IsSimulatingPhysics())
	{
		return;
	}

	if (DebrisProfile.InheritedVelocityScale > 0.0f && !InheritedVelocity.IsNearlyZero())
	{
		DebrisComponent->AddImpulse(
			InheritedVelocity * DebrisProfile.InheritedVelocityScale,
			NAME_None,
			true);
	}

	if (DebrisProfile.RadialImpulseRadius > 0.0f
		&& DebrisProfile.RadialImpulseStrength > 0.0f)
	{
		const FVector ExplosionCenter =
			DebrisComponent->Bounds.Origin
			+ DebrisComponent->GetComponentTransform().TransformVectorNoScale(
				DebrisProfile.LocalExplosionCenterOffset);

		DebrisComponent->AddRadialImpulse(
			ExplosionCenter,
			DebrisProfile.RadialImpulseRadius,
			DebrisProfile.RadialImpulseStrength,
			DebrisProfile.RadialImpulseFalloff,
			DebrisProfile.bIgnoreMass);
	}

	if (!DebrisProfile.LocalDirectionalImpulse.IsNearlyZero())
	{
		const FVector WorldDirectionalImpulse =
			DebrisComponent->GetComponentTransform().TransformVectorNoScale(
				DebrisProfile.LocalDirectionalImpulse);

		DebrisComponent->AddImpulse(
			WorldDirectionalImpulse,
			NAME_None,
			DebrisProfile.bIgnoreMass);
	}

	if (!FMath::IsNearlyZero(DebrisProfile.UpwardImpulseStrength))
	{
		DebrisComponent->AddImpulse(
			FVector::UpVector * DebrisProfile.UpwardImpulseStrength,
			NAME_None,
			DebrisProfile.bIgnoreMass);
	}
}
