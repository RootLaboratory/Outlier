// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/Spawn/WeaponSpawnPoint.h"
#include "Weapon/WeaponBase.h"


// Sets default values
AWeaponSpawnPoint::AWeaponSpawnPoint()
{
 	PrimaryActorTick.bCanEverTick = false;

}

// Called when the game starts or when spawned
void AWeaponSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	SpawnWeapon();
}

void AWeaponSpawnPoint::SpawnWeapon()
{
	if (!HasAuthority() || !WeaponClass || SpawnedWeapon)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;

	SpawnedWeapon = GetWorld()->SpawnActor<AWeaponBase>(
		WeaponClass,
		GetActorTransform(),
		Params
	);

	if (SpawnedWeapon)
	{
		SpawnedWeapon->SetOwningSpawnPoint(this);
	}
}

void AWeaponSpawnPoint::NotifyWeaponRemoved(AWeaponBase* Weapon)
{
	if (!HasAuthority() || Weapon != SpawnedWeapon)
	{
		return;
	}

	SpawnedWeapon = nullptr;

	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle,
		this,
		&AWeaponSpawnPoint::SpawnWeapon,
		RespawnDelay,
		false
	);
}

void AWeaponSpawnPoint::NotifyWeaponPickedUp(AWeaponBase* Weapon)
{
	if (!HasAuthority() || Weapon != SpawnedWeapon)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
}
