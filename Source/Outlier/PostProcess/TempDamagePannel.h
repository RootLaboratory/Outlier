// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TempDamagePannel.generated.h"


class AShooterCharacter;
class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API ATempDamagePannel : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATempDamagePannel();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:

	void OnCollision(AActor* CollidedActor);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PannelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> DamageCollision;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float DamageAmount = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.01"))
	float DamageInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	uint8 bApplyDamageImmediately : 1 = false;

	UPROPERTY(EditAnywhere, Category = "ShooterCharacter")
	TObjectPtr<AShooterCharacter> Temp_Shooter;

private:
	UFUNCTION()
	void HandleDamageCollisionBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);

	UFUNCTION()
	void HandleDamageCollisionEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex
	);

	void ApplyDamageToShooter(AShooterCharacter* Shooter) const;

private:
	
	float DamageAccumulatedTime = 0.0f;

	uint8 IngCollision : 1 = false;
};
