// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/WeaponBase.h"

bool AWeaponBase::CanAttack() const
{
	return false;
}

void AWeaponBase::StartAttack()
{
}

void AWeaponBase::StopAttack()
{
}

void AWeaponBase::PerformAttack()
{
}

void AWeaponBase::OnEquipped(ACharacter* NewOwner)
{
}

void AWeaponBase::OnUnequipped()
{
}
