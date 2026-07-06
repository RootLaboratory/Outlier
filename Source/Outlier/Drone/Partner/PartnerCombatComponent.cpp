// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Weapon/WeaponBase.h"

UPartnerCombatComponent::UPartnerCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPartnerCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (PartnerCharacter && PartnerCharacter->HasAuthority() && bEquipDefaultWeaponOnBeginPlay)
	{
		EquipDefaultWeapon_Server();
	}
}

void UPartnerCombatComponent::TryStartAttack()
{
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		ServerStartAttack();
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->StartAttack();
	}
}

void UPartnerCombatComponent::TryStopAttack()
{
	if (!PartnerCharacter)
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority())
	{
		ServerStopAttack();
		return;
	}

	if (AWeaponBase* Weapon = PartnerCharacter->GetCurrentWeapon())
	{
		Weapon->StopAttack();
	}
}

void UPartnerCombatComponent::ServerStartAttack_Implementation()
{
	TryStartAttack();
}

void UPartnerCombatComponent::ServerStopAttack_Implementation()
{
	TryStopAttack();
}

void UPartnerCombatComponent::EquipDefaultWeapon_Server()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !DefaultWeaponClass || PartnerCharacter->GetCurrentWeapon())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = PartnerCharacter;
	SpawnParams.Instigator = PartnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AWeaponBase* DefaultWeapon = GetWorld()->SpawnActor<AWeaponBase>(
		DefaultWeaponClass,
		PartnerCharacter->GetActorTransform(),
		SpawnParams
	);

	if (DefaultWeapon)
	{
		PartnerCharacter->EquipWeapon(DefaultWeapon);
	}
}
