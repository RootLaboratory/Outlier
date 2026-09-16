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
	Params.OverrideLevel = GetLevel();

	SpawnedWeapon = GetWorld()->SpawnActor<AWeaponBase>(
		WeaponClass,
		GetActorTransform(),
		Params
	);

	// 무기는 더 이상 자기를 스폰한 포인트를 알지 못한다 (AWeaponBase::OwningSpawnPoint 제거).
	// 따라서 아래 NotifyWeaponRemoved / NotifyWeaponPickedUp 은 현재 호출되지 않고,
	// 이 액터는 BeginPlay 에 무기를 한 번 배치하는 역할만 한다 (리스폰 없음).
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

	SpawnedWeapon = nullptr;
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
}
