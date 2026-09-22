#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/Death/OutlierDeathDebrisTypes.h"
#include "TestGeometryCollection.generated.h"

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
