// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Shooter/ShooterCharacter.h"

namespace
{
	const TCHAR* NetPrefix(const AActor* Actor)
	{
		return (Actor && Actor->HasAuthority()) ? TEXT("[Server]") : TEXT("[Client]");
	}
}

void ARangedWeaponBase::StartAttackCooldown()
{
	bAttackOnCooldown = true;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			AttackCooldownTimerHandle,
			this,
			&ARangedWeaponBase::ResetAttackCooldown,
			AttackInterval,
			false
		);
	}
}

void ARangedWeaponBase::ResetAttackCooldown()
{
	bAttackOnCooldown = false;
}

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
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Reload blocked Ammo=%d Reserve=%d Reloading=%d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsReloading ? 1 : 0);
		return;
	}

	bIsReloading = true;

	const int32 NeededAmmo = MagazineSize - CurrentAmmo;
	const int32 AmmoToLoad = FMath::Min(NeededAmmo, ReserveAmmo);

	CurrentAmmo += AmmoToLoad;
	ReserveAmmo -= AmmoToLoad;
	bIsReloading = false;

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Reload complete Ammo=%d / %d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
}

void ARangedWeaponBase::ConsumeAmmo()
{
	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);

	if (CurrentAmmo == 0 && CanReload())
	{
		Reload();
	}
}


void ARangedWeaponBase::FireShot()
{
	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: WeaponOwner is null"), NetPrefix(this), *GetName());
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: owner cast failed"), NetPrefix(this), *GetName());
		return;
	}

	if (!OwnerCharacter->GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: controller is null"), NetPrefix(this), *GetName());
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
		ECC_PhysicsBody,
		Params
	);

	if (bHit)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot hit Target=%s Start=%s End=%s"), NetPrefix(this), *GetName(), *GetNameSafe(Hit.GetActor()), *Start.ToString(), *End.ToString());
		if (AShooterCharacter* HitCharacter = Cast<AShooterCharacter>(Hit.GetActor()))
		{
			HitCharacter->ApplyDamageInternal(Damage);
			UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot applied Damage=%.1f To=%s"), NetPrefix(this), *GetName(), Damage, *GetNameSafe(HitCharacter));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot miss Start=%s End=%s"), NetPrefix(this), *GetName(), *Start.ToString(), *End.ToString());
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

void ARangedWeaponBase::HandleAutoFire()
{
	if (!bIsAttacking || !Super::CanAttack() || bIsReloading || CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire stop Attack=%d BaseCanAttack=%d Reloading=%d Ammo=%d"), NetPrefix(this), *GetName(), bIsAttacking ? 1 : 0, Super::CanAttack() ? 1 : 0, bIsReloading ? 1 : 0, CurrentAmmo);
		StopAttack();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire tick Ammo=%d Reserve=%d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
	PerformAttack();
}

bool ARangedWeaponBase::CanAttack() const
{
	return Super::CanAttack()
		&& !bAttackOnCooldown
		&& !bIsReloading
		&& CurrentAmmo > 0;
}

void ARangedWeaponBase::StartAttack()
{
	if (bIsAttacking)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack skipped: already attacking"), NetPrefix(this), *GetName());
		return;
	}

	if (!CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] StartAttack blocked CanAttack=false Ammo=%d Reserve=%d Reloading=%d Cooldown=%d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsReloading ? 1 : 0, bAttackOnCooldown ? 1 : 0);
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s [%s] No ammo"), NetPrefix(this), *GetName());
		}
		return;
	}

	PerformAttack(); // 첫 발 즉시 발사

	UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack Ammo=%d Reserve=%d Automatic=%d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsAutomatic ? 1 : 0);
	bIsAttacking = true;
	// Attack state is set by ranged fire flow.

	if (bIsAutomatic)
	{
		GetWorld()->GetTimerManager().SetTimer(
			AutoFireTimerHandle,
			this,
			&ARangedWeaponBase::HandleAutoFire,
			AttackInterval,
			true
		);
	}
}

void ARangedWeaponBase::StopAttack()
{
	Super::StopAttack();

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoFireTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(AttackCooldownTimerHandle);
	}

	bAttackOnCooldown = false;
	UE_LOG(LogTemp, Log, TEXT("%s [%s] StopAttack cleared timers"), NetPrefix(this), *GetName());
}

void ARangedWeaponBase::PerformAttack()
{
	if (!Super::CanAttack() || bIsReloading || CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] PerformAttack blocked BaseCanAttack=%d Reloading=%d Ammo=%d"), NetPrefix(this), *GetName(), Super::CanAttack() ? 1 : 0, bIsReloading ? 1 : 0, CurrentAmmo);
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s [%s] No ammo"), NetPrefix(this), *GetName());
			StopAttack();
		}
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] PerformAttack AmmoBefore=%d Reserve=%d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
	ConsumeAmmo();
	FireShot();

	StartAttackCooldown();

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Fire success Ammo=%d / %d"), NetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
}
