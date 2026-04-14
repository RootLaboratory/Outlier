// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/MeleeWeaponBase.h"

AMeleeWeaponBase::AMeleeWeaponBase()
{
	WeaponType = EWeaponType::Melee;
}

void AMeleeWeaponBase::PerformAttack()
{
}

void AMeleeWeaponBase::TraceMeleeHit()
{
}

void AMeleeWeaponBase::ApplyHitToTarget(AActor* Target)
{
}
