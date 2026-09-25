#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "Interface/JumperAffectableInterface.h"
#include "Jumper.generated.h"

class UBoxComponent;
class UHackableComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API AJumper : public AActor, public IHackableInterface
{
	GENERATED_BODY()

public:
	AJumper();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jumper|Components")
	TObjectPtr<UStaticMeshComponent> JumperMesh;

	/** Always-queryable collision used by the hack candidate search. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jumper|Components")
	TObjectPtr<UBoxComponent> HackTargetCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jumper")
	TObjectPtr<UBoxComponent> JumperVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jumper")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper")
	FName EffectId = TEXT("Jumper.Direction");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper")
	FVector LocalDirection = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper", meta = (ClampMin = "0.0"))
	float AscendMultiplier = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper", meta = (ClampMin = "0.0"))
	float DescendMultiplier = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper|Partner", meta = (ClampMin = "0.0"))
	float PartnerFlightMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper|Debug")
	bool bEnableVolumeOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jumper|Debug")
	bool bDebugJumper = true;

private:
	UFUNCTION()
	void HandleVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	void SetJumperActive(bool bActive);
	FJumperMovementEffect BuildMovementEffect() const;
	TSet<TWeakObjectPtr<AActor>> OverlappingAffectableActors;
	bool bJumperActive = false;
};
