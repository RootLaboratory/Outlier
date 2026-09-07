#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "TestGeometryCollection.generated.h"

class UGeometryCollectionComponent;

/**
 * Blueprint-tunable, cosmetic-only physics applied when the test enemy dies.
 *
 * Strength values are treated as velocity changes when bIgnoreMass is enabled.
 */
USTRUCT(BlueprintType)
struct FGeometryCollectionDeathProfile
{
	GENERATED_BODY()

	/** Release the authored child clusters/pieces from their active root cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Break")
	bool bCrumbleRootOnDeath = true;

	/** Local-space offset from the Geometry Collection origin used by the radial kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	FVector LocalExplosionCenterOffset = FVector::ZeroVector;

	/** Portion of the enemy's pre-death velocity inherited by every released piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "0.0"))
	float InheritedVelocityScale = 0.8f;

	/** Radius of the radial kick in centimeters. Zero disables the radial kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "0.0", Units = "cm"))
	float RadialImpulseRadius = 250.0f;

	/** Outward radial kick. With bIgnoreMass enabled, this behaves as delta velocity in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "0.0"))
	float RadialImpulseStrength = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	TEnumAsByte<ERadialImpulseFalloff> RadialImpulseFalloff = RIF_Linear;

	/** Additional kick expressed in the Geometry Collection's local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	FVector LocalDirectionalImpulse = FVector::ZeroVector;

	/** Additional world-up kick applied to every released piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	float UpwardImpulseStrength = 75.0f;

	/** Ignore mass so the values above are easy to art-direct as velocity changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	bool bIgnoreMass = true;

	/** Existing collision profile used as the base response table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FName CollisionProfileName = TEXT("PhysicsActor");

	/** Cosmetic debris does not need to participate in scene queries by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bEnableSceneQueries = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bIgnorePawnCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bIgnoreCameraCollision = true;

	/** Enable the cheaper one-way debris interaction after the base setup is verified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bEnableOneWayInteraction = false;

	/** Cluster level at which debris becomes one-way when the option above is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0", EditCondition = "bEnableOneWayInteraction"))
	int32 OneWayInteractionLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bEnableGravity = true;

	/** Seconds before the entire cosmetic debris actor is removed. Zero keeps it indefinitely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifecycle", meta = (ClampMin = "0.0", Units = "s"))
	float DebrisLifetime = 10.0f;
};

/**
 * Temporary enemy used to test a pre-fractured Geometry Collection on death.
 */
UCLASS()
class OUTLIER_API ATestGeometryCollection : public AEnemyBase
{
	GENERATED_BODY()

public:
	ATestGeometryCollection();

protected:
	virtual void BeginPlay() override;
	virtual void HandleDeath() override;

	/** Assign the fractured Rest Collection on a Blueprint derived from this class. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Death")
	TObjectPtr<UGeometryCollectionComponent> DeathGeometryCollectionComponent;

	/** Runtime behavior for the cosmetic Geometry Collection debris. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Death")
	FGeometryCollectionDeathProfile DeathProfile;

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastActivateDeathGeometry(FVector_NetQuantize100 DeathVelocity);

	void ActivateDeathGeometry(const FVector& DeathVelocity);
	void ReleaseDeathGeometry(const FVector& DeathVelocity);
	void ApplyDeathImpulses(const FVector& DeathVelocity);
};
