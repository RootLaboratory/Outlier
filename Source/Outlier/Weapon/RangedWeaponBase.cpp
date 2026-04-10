// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"
#include "GameFramework/Character.h"

bool ARangedWeaponBase::CanReload() const
{
	return !bIsReloading
		&& CurrentAmmo < MagazineSize
		&& ReserveAmmo > 0;
}

void ARangedWeaponBase::Reload()
{
	if (!CanReload())
	{
		return;
	}

	bIsReloading = true;

	const int32 NeededAmmo = MagazineSize - CurrentAmmo;
	const int32 AmmoToLoad = FMath::Min(NeededAmmo, ReserveAmmo);

	CurrentAmmo += AmmoToLoad;
	ReserveAmmo -= AmmoToLoad;
	bIsReloading = false;

	UE_LOG(LogTemp, Log, TEXT("[%s] Reloadeed. Ammo : %d / %d"), *GetName(), CurrentAmmo, ReserveAmmo);
}

void ARangedWeaponBase::ConsumeAmmo()
{
	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);
}

void ARangedWeaponBase::FireShot()
{
	if (!WeaponOwner)
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;

	OwnerCharacter->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	FVector Start = CameraLocation;
	FVector End = Start + (CameraRotation.Vector() * EffectiveRange); // 사거리


	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(OwnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);

	if (bHit)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] Hit : %s"), *GetName(), *GetNameSafe(Hit.GetActor()));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] Miss"), *GetName());
	}

	FColor LineColor = bHit ? FColor::Green : FColor::Red;

	DrawDebugLine(
		GetWorld(),
		Start,
		End,
		LineColor,
		false,
		3.0f,
		0,
		1.0f
	);
}

// 반동, 탄 퍼짐은 추후 작업 예정
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
	return Super::CanAttack()
		&& !bIsReloading
		&& CurrentAmmo > 0;
}

void ARangedWeaponBase::StartAttack()
{
	if (!CanAttack())
	{
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] No ammo"), *GetName());
		}
		return;
	}

	Super::StartAttack();
}

void ARangedWeaponBase::StopAttack()
{
	Super::StopAttack();
}

void ARangedWeaponBase::PerformAttack()
{
	ConsumeAmmo();
	FireShot();

	bAttackOnCooldown = true;
	bIsAttacking = false;

	UE_LOG(LogTemp, Log, TEXT("[%s] Fire success. Ammo: %d / %d"), *GetName(), CurrentAmmo, ReserveAmmo);
}
