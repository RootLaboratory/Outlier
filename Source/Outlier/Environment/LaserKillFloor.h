#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaserKillFloor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API ALaserKillFloor : public AActor
{
	GENERATED_BODY()

public:
	ALaserKillFloor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Hazard|Laser Floor")
	void SetHazardEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Hazard|Laser Floor")
	bool IsHazardEnabled() const { return bHazardEnabled; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Laser Floor")
	TObjectPtr<UBoxComponent> KillVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard|Laser Floor")
	TObjectPtr<UStaticMeshComponent> LaserPlane;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|Laser Floor")
	TObjectPtr<UMaterialInterface> LaserMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|Laser Floor", meta = (ClampMin = "0.0"))
	float InstantKillDamage = 1000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard|Laser Floor")
	uint8 bVisibleOnlyWhenEnabled : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_HazardEnabled, Category = "Hazard|Laser Floor")
	uint8 bHazardEnabled : 1 = true;

private:
	UFUNCTION()
	void HandleKillVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleKillVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UFUNCTION()
	void OnRep_HazardEnabled();

	void ApplyHazardState();
	void ApplyInstantKillDamage(AActor* TargetActor, const FHitResult& HitResult);
	void DamageCurrentOverlaps();
	TSet<TWeakObjectPtr<AActor>> DamagedActorsInVolume;
};
