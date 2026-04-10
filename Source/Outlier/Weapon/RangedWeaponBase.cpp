// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"

bool ARangedWeaponBase::CanReload() const
{
	if(CurrentAmmo == MagazineSize || ReserveAmmo == 0)
		return false;

	return true;
}

void ARangedWeaponBase::Reload()
{
	if (ReserveAmmo >= MagazineSize)
	{
		int32 RemainAmmo = CurrentAmmo;
		CurrentAmmo = MagazineSize;
		ReserveAmmo = ReserveAmmo - MagazineSize + RemainAmmo;
	}
	else
	{
		int32 RemainAmmo = CurrentAmmo;
		CurrentAmmo += ReserveAmmo;
		if (CurrentAmmo > MagazineSize)
		{
			ReserveAmmo = CurrentAmmo - MagazineSize;
		}
		else
		{
			ReserveAmmo = 0;
		}
	}
}

void ARangedWeaponBase::ConsumeAmmo()
{
}

void ARangedWeaponBase::FireShot()
{
}

void ARangedWeaponBase::ApplyRecoil()
{
}

void ARangedWeaponBase::ApplyBloomPerShot()
{
}

void ARangedWeaponBase::RecoverBloom(float DeltaTime)
{
}

float ARangedWeaponBase::GetCurrentSpread() const
{
	return 0.0f;
}

void ARangedWeaponBase::SetAiming(bool Aimming)
{
}

bool ARangedWeaponBase::CanAttack() const
{
	return false;
}

void ARangedWeaponBase::StartAttack()
{

}

void ARangedWeaponBase::StopAttack()
{

}

void ARangedWeaponBase::PerformAttack()
{

}
