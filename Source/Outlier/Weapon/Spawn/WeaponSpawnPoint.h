// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponSpawnPoint.generated.h"

class AWeaponBase;

UCLASS()
class OUTLIER_API AWeaponSpawnPoint : public AActor
{
	GENERATED_BODY()
	
public:	
	AWeaponSpawnPoint();

protected:
	UPROPERTY(EditAnywhere, Category = "Weapon")
	TSubclassOf<AWeaponBase> WeaponClass;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float RespawnDelay = 10.0f;

	UPROPERTY()
	TObjectPtr<AWeaponBase> SpawnedWeapon;

	FTimerHandle RespawnTimerHandle;

protected:
	virtual void BeginPlay() override;

public:
	void SpawnWeapon();
	void NotifyWeaponRemoved(AWeaponBase* Weapon);
	void NotifyWeaponPickedUp(AWeaponBase* Weapon);
};
