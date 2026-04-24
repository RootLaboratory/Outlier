// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "VisualEventSubsystem.h"
#include "ProjectionMarkDefinition.h"
#include "TrailEffectDefinition.h"
#include "SoundDefinition.h"
#include "VisualEffectProvider.h"
#include "MainUIBase.h"
#include "LocalPlayerUISubSystem.h"
#include "CrossHairBase.h"
#include "Shooter/ShooterPlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierNetUtils.h"

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

void ARangedWeaponBase::StartReuseCooldown()
{
	if (ReuseCooldown <= 0.0f || !GetWorld())
	{
		return;
	}

	bOnReuseCooldown = true;
	GetWorld()->GetTimerManager().ClearTimer(ReuseCooldownTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		ReuseCooldownTimerHandle,
		this,
		&ARangedWeaponBase::FinishReuseCooldown,
		ReuseCooldown,
		false
	);
}

void ARangedWeaponBase::FinishReuseCooldown()
{
	bOnReuseCooldown = false;
}

bool ARangedWeaponBase::CanReload() const
{
	return !bIsReloading
		&& CurrentAmmo < MagazineSize
		&& ReserveAmmo > 0;
}

void ARangedWeaponBase::Reload()
{
	BeginReload();
}

void ARangedWeaponBase::BeginReload()
{
	if (!CanReload())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Reload blocked Ammo=%d Reserve=%d Reloading=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsReloading ? 1 : 0);
		return;
	}

	bIsReloading = true;
}

void ARangedWeaponBase::FinishReload()
{
	if (!bIsReloading)
	{
		return;
	}

	const int32 NeededAmmo = MagazineSize - CurrentAmmo;
	const int32 AmmoToLoad = FMath::Min(NeededAmmo, ReserveAmmo);

	CurrentAmmo += AmmoToLoad;
	ReserveAmmo -= AmmoToLoad;
	bIsReloading = false;

	if (GetLocalUISubsystem() != nullptr)
	{
		GetLocalUISubsystem()->OnRep_AmmoCountChanged(CurrentAmmo);
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Reload complete Ammo=%d / %d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
}

void ARangedWeaponBase::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	bIsReloading = false;
}

void ARangedWeaponBase::ConsumeAmmo()
{
	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);

	if (GetLocalUISubsystem() != nullptr)
	{
		GetLocalUISubsystem()->OnRep_AmmoCountChanged(CurrentAmmo);
	}

	if (CurrentAmmo == 0 && CanReload())
	{
		BeginReload();
	}
}


void ARangedWeaponBase::FireShot()
{
	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: WeaponOwner is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: owner cast failed"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!OwnerCharacter->GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] FireShot blocked: controller is null"), OutlierNet::GetNetPrefix(this), *GetName());
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

	DrawDebugLine(
		GetWorld(),
		Start,
		End,
		FColor::Red,
		false,   // PersistentLines
		3.0f,    // LifeTime
		0,       // DepthPriority
		1.0f     // Thickness
	);

	if (bHit)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot hit Target=%s Start=%s End=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Hit.GetActor()), *Start.ToString(), *End.ToString());
		if (AShooterCharacter* HitCharacter = Cast<AShooterCharacter>(Hit.GetActor()))
		{
			HitCharacter->ApplyDamageInternal(Damage);
			UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot applied Damage=%.1f To=%s"), OutlierNet::GetNetPrefix(this), *GetName(), Damage, *GetNameSafe(HitCharacter));
		}
		
		//GetLocalUISubsystem()->OnRep_AttackSign(EAttackSign::Default); //Enemy 추가되면 확장
		//GetLocalUISubsystem()->OnRep_Aiming(); // Aiming 확장되면 추가. 기능은 구현 완.
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot miss Start=%s End=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *Start.ToString(), *End.ToString());
	}

	{
		GetLocalUISubsystem()->OnRep_ShootCrosshairChanged();

		AActor* HitActor = Hit.GetActor();
		MulticastPlayFireFX_Implementation(Hit.ImpactPoint, HitActor);

		if(UVisualEventSubsystem * VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
		{
			if (GunSound)
			{
				VisualSubsystem->PlaySoundAtLocation(GunSound, Start);
			}
		}

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


void ARangedWeaponBase::MulticastPlayFireFX_Implementation(FVector_NetQuantize TraceEnd,  AActor* Hit)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	if (!Shooter)
	{
		return;
	}

	if (Shooter->IsLocallyControlled())
	{
		PlayFirstPersonFireFX(TraceEnd, Hit);
		return;
	}

	PlayThirdPersonFireFX(TraceEnd, Hit);
}

void ARangedWeaponBase::PlayThirdPersonFireFX(FVector TraceEnd,  AActor* Hit)
{
	USkeletalMeshComponent* Mesh = ThirdPersonWeaponMesh;
	if (!Mesh)
	{
		return;
	}

	if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
	{
		FVector MuzzleLocation = Mesh->GetSocketLocation(TEXT("Muzzle"));
		FVector MuzzleForward = Mesh->GetSocketRotation(TEXT("Muzzle")).Vector();
		const FRotator MuzzleRotation = Mesh->GetSocketRotation(TEXT("Muzzle"));

		FVector Start = MuzzleLocation + MuzzleForward * 10.f;
		FVector End = TraceEnd;

		if (WeaponMuzzle)
		{
			//UE_LOG(LogTemp, Log, TEXT("PlayFirstPersonFireFX_EffectSpawned"));
			//UTrailEffectDefinition* MuzzleEffectInstance = NewObject<UTrailEffectDefinition>(this, WeaponMuzzle);

			if (!Hit)
			{

				FVector CameraLocation;
				FRotator CameraRotation;

				ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
				OwnerCharacter->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

				Start = CameraLocation;
			}

			VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);

		}


		if (WeaponTrail)
		{
			//UTrailEffectDefinition* TrailEffectInstance = NewObject<UTrailEffectDefinition>(this, WeaponTrail);
			if (!Hit)
			{

				FVector CameraLocation;
				FRotator CameraRotation;

				ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
				OwnerCharacter->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

				Start = CameraLocation;
				End = Start + (CameraRotation.Vector() * EffectiveRange);

			}

			VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, End);
		}


		if (Hit)
		{
			//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit"));
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);

			if (Provider)
			{
				//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit and ProviderComponent"));
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				VisualSubsystem->FeaturesEffect(TraceEnd, MuzzleRotation, AssetSet);
			}

			else
			{
				//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit but no  ProviderComponent"));
			}

		}

	}
}

void ARangedWeaponBase::PlayFirstPersonFireFX(FVector TraceEnd, AActor* Hit)
{

	USkeletalMeshComponent* Mesh = FirstPersonWeaponMesh;
	if (!Mesh)
	{
		return;
	}


	if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
	{
		FVector MuzzleLocation = Mesh->GetSocketLocation(TEXT("Muzzle"));
		FVector MuzzleForward = Mesh->GetSocketRotation(TEXT("Muzzle")).Vector();
		const FRotator MuzzleRotation = Mesh->GetSocketRotation(TEXT("Muzzle"));

		FVector Start = MuzzleLocation + MuzzleForward * 10.f;
		FVector End = TraceEnd;

		if (WeaponMuzzle)
		{
			//UE_LOG(LogTemp, Log, TEXT("PlayFirstPersonFireFX_EffectSpawned"));
			//UTrailEffectDefinition* MuzzleEffectInstance = NewObject<UTrailEffectDefinition>(this, WeaponMuzzle);

			if (!Hit)
			{

				FVector CameraLocation;
				FRotator CameraRotation;

				ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
				OwnerCharacter->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

				Start = CameraLocation;
			}

			VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);
			
		}


		if (WeaponTrail)
		{
			//UTrailEffectDefinition* TrailEffectInstance = NewObject<UTrailEffectDefinition>(this, WeaponTrail);
			if (!Hit)
			{

				FVector CameraLocation;
				FRotator CameraRotation;

				ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
				OwnerCharacter->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

				Start = CameraLocation;
				End = Start + (CameraRotation.Vector() * EffectiveRange);

			}

			VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, End);
		}


		if (Hit)
		{
			//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit"));
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);

			if (Provider)
			{
				//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit and ProviderComponent"));
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				VisualSubsystem->FeaturesEffect(TraceEnd, MuzzleRotation, AssetSet);
			}

			else
			{
				//UE_LOG(LogTemp, Error, TEXT("PlayFirstPersonFireFX Hit but no  ProviderComponent"));
			}

		}

	}
}

ULocalPlayerUISubSystem* ARangedWeaponBase::GetLocalUISubsystem()
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);

	if (Shooter)
	{
		AShooterPlayerController* Player = Cast<AShooterPlayerController>(Shooter->GetController());
		if (!Player)
		{
			return nullptr;
		}

		if (ULocalPlayer* LP = Player->GetLocalPlayer())
		{
			if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
			{
				return UISubsystem;
			}
			else
			{
				return nullptr;
			}
		}
	}
	return nullptr;
}

void ARangedWeaponBase::HandleAutoFire()
{
	if (!bIsAttacking || !Super::CanAttack() || bIsReloading || CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire stop Attack=%d BaseCanAttack=%d Reloading=%d Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), bIsAttacking ? 1 : 0, Super::CanAttack() ? 1 : 0, bIsReloading ? 1 : 0, CurrentAmmo);
		StopAttack();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire tick Ammo=%d Reserve=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
	PerformAttack();
}

bool ARangedWeaponBase::CanAttack() const
{
	return Super::CanAttack()
		&& !bAttackOnCooldown
		&& !bOnReuseCooldown
		&& !bIsReloading
		&& CurrentAmmo > 0;
}

void ARangedWeaponBase::StartAttack()
{
	if (bIsAttacking)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack skipped: already attacking"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] StartAttack blocked CanAttack=false Ammo=%d Reserve=%d Reloading=%d Cooldown=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsReloading ? 1 : 0, bAttackOnCooldown ? 1 : 0);
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s [%s] No ammo"), OutlierNet::GetNetPrefix(this), *GetName());
		}
		return;
	}

	PerformAttack(); // 첫 발 즉시 발사

	UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack Ammo=%d Reserve=%d Automatic=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo, bIsAutomatic ? 1 : 0);
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
	UE_LOG(LogTemp, Log, TEXT("%s [%s] StopAttack cleared timers"), OutlierNet::GetNetPrefix(this), *GetName());
}

void ARangedWeaponBase::PerformAttack()
{
	if (!Super::CanAttack() || bIsReloading || CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] PerformAttack blocked BaseCanAttack=%d Reloading=%d Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), Super::CanAttack() ? 1 : 0, bIsReloading ? 1 : 0, CurrentAmmo);
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s [%s] No ammo"), OutlierNet::GetNetPrefix(this), *GetName());
			StopAttack();
		}
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] PerformAttack AmmoBefore=%d Reserve=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
	ConsumeAmmo();
	FireShot();

	StartAttackCooldown();
	StartReuseCooldown();

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Fire success Ammo=%d / %d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, ReserveAmmo);
}
