#pragma once

#include "CoreMinimal.h"
#include "Enemy/VECDrone.h"
#include "SelfDestructDrone.generated.h"

class UExplosionComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API ASelfDestructDrone : public AVECDrone
{
	GENERATED_BODY()

public:
	ASelfDestructDrone();

	virtual float GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const override;

	// StateTree 전조가 끝났을 때 호출한다. 현재 HP를 0으로 만들고 즉시 폭발 및 사망 처리한다.
	UFUNCTION(BlueprintCallable, Category = "Self Destruct Drone")
	void TriggerSelfDestruct();

	UFUNCTION(BlueprintPure, Category = "Self Destruct Drone")
	UExplosionComponent* GetExplosionComponent() const { return ExplosionComponent; }

protected:
	virtual void HandleDeath() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<USceneComponent> MountedExplosiveRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<UStaticMeshComponent> MountedExplosiveMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<USphereComponent> MountedExplosiveHitbox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Self Destruct Drone|Explosive")
	TObjectPtr<UExplosionComponent> ExplosionComponent;

private:
	bool bDeathHandling = false;
};
