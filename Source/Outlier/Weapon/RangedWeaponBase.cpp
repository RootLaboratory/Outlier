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
#include "Drone/Partner/PartnerCharacter.h"
#include "OutlierNetUtils.h"
#include "Drone/Partner/PartnerShieldSphere.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/WeaponCoreRow.h"
#include "Weapon/WeaponBloomRow.h"
#include "Weapon/WeaponFeedbackDefinition.h"
#include "Weapon/WeaponProjectileRow.h"
#include "Weapon/WeaponRecoilRow.h"

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
		&& CurrentAmmo < MagazineSize;
}

void ARangedWeaponBase::Reload()
{
	BeginReload();
}

void ARangedWeaponBase::BeginReload()
{
	if (!CanReload())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] Reload blocked Ammo=%d Reloading=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, bIsReloading ? 1 : 0);
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
	const int32 AmmoToLoad = FMath::Min(NeededAmmo, MagazineSize);

	CurrentAmmo += AmmoToLoad;
	bIsReloading = false;

	UpdateLocalAmmoUI();

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Reload complete Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo);
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
	UpdateLocalAmmoUI();
}


void ARangedWeaponBase::FireShot()
{
	if (!HasAuthority())
	{
		return;
	}

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
		const float HitDistance = FVector::Distance(Start, Hit.ImpactPoint);
		const float DamageToApply = GetDamageAtDistance(HitDistance);

		if (APartnerShieldSphere* Shield = Cast<APartnerShieldSphere>(Hit.GetActor()))
		{
			Shield->ApplyShieldDamage(DamageToApply);
			UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot applied Shield Damage=%.1f To=%s"), OutlierNet::GetNetPrefix(this), *GetName(), DamageToApply, *GetNameSafe(Shield));
		}
		else if (AShooterCharacter* HitCharacter = Cast<AShooterCharacter>(Hit.GetActor()))
		{
			HitCharacter->ApplyDamageInternal(DamageToApply);
			UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot applied Damage=%.1f To=%s"), OutlierNet::GetNetPrefix(this), *GetName(), DamageToApply, *GetNameSafe(HitCharacter));
		}
		else if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(Hit.GetActor()))
		{
			PartnerCharacter->HandlePartnerHit();
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] FireShot miss Start=%s End=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *Start.ToString(), *End.ToString());
	}


	ClientNotifyShotFired();

	{
		AActor* HitActor = Hit.GetActor();
		const FVector TraceEndPoint = bHit ? Hit.ImpactPoint : End;
		MulticastPlayFireFX(TraceEndPoint, HitActor);

		if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
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
	BloomCurrent = FMath::Clamp(BloomCurrent + BloomPerShot, BloomMin, BloomMax);
}

void ARangedWeaponBase::RecoverBloom(float DeltaTime)
{
	BloomCurrent = FMath::FInterpConstantTo(BloomCurrent, BloomMin, DeltaTime, BloomRecoveryRate);
}

float ARangedWeaponBase::GetCurrentSpread() const
{
	return BloomCurrent;
}

void ARangedWeaponBase::SetAiming(bool bAiming)
{
	bIsAiming = bAiming;

	RefreshBloomSettingsFromState();
}


void ARangedWeaponBase::MulticastPlayFireFX_Implementation(FVector_NetQuantize TraceEnd, AActor* Hit)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		return;
	}

	if (OwnerCharacter->IsLocallyControlled())
	{
		PlayFirstPersonFireFX(TraceEnd, Hit);
		return;
	}

	PlayThirdPersonFireFX(TraceEnd, Hit);
}


void ARangedWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	UpdateLocalAmmoUI();
}

void ARangedWeaponBase::OnRep_CurAmmo()
{
	UpdateLocalAmmoUI();
}

void ARangedWeaponBase::OnRep_EquippedState()
{
	Super::OnRep_EquippedState();

	if (IsEquipped())
	{
		UpdateLocalAmmoUI();
	}
}

void ARangedWeaponBase::ClientNotifyShotFired_Implementation()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
		{
			UISubsystem->OnRep_ShootCrosshairChanged(ReuseCooldown);
		}
	}
}

void ARangedWeaponBase::PlayThirdPersonFireFX(FVector TraceEnd, AActor* Hit)
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

		const FVector Start = MuzzleLocation;// +MuzzleForward * 10.f;
		FVector End = TraceEnd;

		if (WeaponMuzzle)
		{
			VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);

		}


		if (WeaponTrail)
		{
			VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, End);
		}


		if (Hit)
		{
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);

			if (Provider)
			{
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				VisualSubsystem->FeaturesEffect(TraceEnd, MuzzleRotation, AssetSet);
			}

			else
			{
				if (WeaponDecal)
				{
					VisualSubsystem->SpawnMarkAtLocation(WeaponDecal, Start, MuzzleRotation);
				}
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

		const FVector Start = MuzzleLocation + MuzzleForward * 10.f;
		FVector End = TraceEnd;

		if (WeaponMuzzle)
		{
			VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);
		}


		if (WeaponTrail)
		{
			VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, End);
		}

		if (Hit)
		{
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);

			if (Provider)
			{
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				VisualSubsystem->FeaturesEffect(TraceEnd, MuzzleRotation, AssetSet);
			}

			else
			{
				if (WeaponDecal)
				{
					VisualSubsystem->SpawnMarkAtLocation(WeaponDecal, Start, MuzzleRotation);
			    }
			}

		}

	}
}

ULocalPlayerUISubSystem* ARangedWeaponBase::GetLocalUISubsystem() const
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

void ARangedWeaponBase::UpdateLocalAmmoUI() const
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_AmmoCountChanged(CurrentAmmo);
	}
}

void ARangedWeaponBase::InitializeFromDataTables()
{
	Super::InitializeFromDataTables();

	if (const FWeaponCoreRow* CoreRow = WeaponCoreRow.GetRow<FWeaponCoreRow>(TEXT("InitializeRangedWeaponCore")))
	{
		MagazineSize = FMath::Max(CoreRow->MagazineSize, 0);
		CurrentAmmo = MagazineSize;
		ReloadTime = FMath::Max(CoreRow->ReloadTime, 0.0f);
		BloomMin = CoreRow->DefaultMinBloom;
		BloomMax = FMath::Max(CoreRow->DefaultMaxBloom, BloomMin);
		BloomCurrent = FMath::Clamp(BloomCurrent, BloomMin, BloomMax);
		RecoilMultiplier = FMath::Max(CoreRow->RecoilMultiplier, 0.0f);
		bIsAutomatic = FireMode == EWeaponFireMode::FullAuto;
		BloomProfileId = CoreRow->BloomProfileId;
		ProjectileProfileId = CoreRow->ProjectileProfileId;
		RecoilProfileId = CoreRow->RecoilProfileId;

		if (CoreRow->FeedbackDefinition)
		{
			FeedbackDefinition = CoreRow->FeedbackDefinition;
		}
	}

	InitializeBloomFromDataTable();
	InitializeRecoilFromDataTable();
	InitializeProjectileFromDataTable();
	ApplyFeedbackDefinition();
}

void ARangedWeaponBase::InitializeBloomFromDataTable()
{
	RefreshBloomSettingsFromState();
}

void ARangedWeaponBase::InitializeRecoilFromDataTable()
{
	const FWeaponRecoilRow* RecoilRow = RecoilDataRow.GetRow<FWeaponRecoilRow>(TEXT("InitializeWeaponRecoil"));
	if (!RecoilRow && WeaponRecoilTable && !RecoilProfileId.IsNone())
	{
		RecoilRow = WeaponRecoilTable->FindRow<FWeaponRecoilRow>(RecoilProfileId, TEXT("InitializeWeaponRecoil"));
	}

	if (!RecoilRow)
	{
		return;
	}

	RecoilPitchAmplitude = RecoilRow->PitchAmplitude;
	RecoilLocationXAmplitude = RecoilRow->LocationXAmplitude;
	RecoilLocationYAmplitude = RecoilRow->LocationYAmplitude;
	RecoilFovAmplitude = RecoilRow->FovAmplitude;
	RecoilRecoverySpeed = RecoilRow->RecoverySpeed;
}

void ARangedWeaponBase::InitializeProjectileFromDataTable()
{// TODO:
	const FWeaponProjectileRow* ProjectileRow = ProjectileDataRow.GetRow<FWeaponProjectileRow>(TEXT("InitializeWeaponProjectile"));
	if (!ProjectileRow && WeaponProjectileTable && !ProjectileProfileId.IsNone())
	{
		ProjectileRow = WeaponProjectileTable->FindRow<FWeaponProjectileRow>(ProjectileProfileId, TEXT("InitializeWeaponProjectile"));
	}

	if (!ProjectileRow)
	{
		return;
	}

	ProjectileSpeedCmPerSec = ProjectileRow->ProjectileSpeedCmPerSec;
	ProjectileMaxRangeCm = ProjectileRow->MaxRangeCm;
	ProjectileStunTime = ProjectileRow->StunTime;

	if (ProjectileRow->ReuseCooldown > 0.0f)
	{
		ReuseCooldown = ProjectileRow->ReuseCooldown;
	}
}

void ARangedWeaponBase::ApplyFeedbackDefinition()
{
	if (!FeedbackDefinition)
	{
		return;
	}

	WeaponDecal  = FeedbackDefinition->WeaponDecal;
	WeaponMuzzle = FeedbackDefinition->WeaponMuzzle;
	WeaponTrail  = FeedbackDefinition->WeaponTrail;
	GunSound     = FeedbackDefinition->GunSound;
}

void ARangedWeaponBase::RefreshBloomSettingsFromState()
{
	if (!WeaponBloomTable || BloomProfileId.IsNone())
	{
		return;
	}

	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	const EWeaponAimMode DesiredAimMode = bIsAiming ? EWeaponAimMode::ADS : EWeaponAimMode::Hip;
	EWeaponMoveState DesiredMoveState = EWeaponMoveState::StillOrCrouch;

	if (Shooter)
	{
		switch (Shooter->GetMovementState())
		{
		case EMovementState::Jump:
			DesiredMoveState = EWeaponMoveState::Air;
			break;
		case EMovementState::Walk:
		case EMovementState::Run:
		case EMovementState::Slide:
			DesiredMoveState = EWeaponMoveState::Moving;
			break;
		case EMovementState::Idle:
		case EMovementState::Crouch:
		default:
			DesiredMoveState = EWeaponMoveState::StillOrCrouch;
			break;
		}
	}

	TArray<FWeaponBloomRow*> BloomRows;
	WeaponBloomTable->GetAllRows<FWeaponBloomRow>(TEXT("RefreshBloomSettingsFromState"), BloomRows);

	const FWeaponBloomRow* BestRow = nullptr;
	for (const FWeaponBloomRow* Row : BloomRows)
	{
		if (!Row || Row->BloomProfileId != BloomProfileId || Row->MoveState != DesiredMoveState)
		{
			continue;
		}

		if (Row->AimMode == DesiredAimMode)
		{
			BestRow = Row;
			break;
		}

		if (Row->AimMode == EWeaponAimMode::Any)
		{
			BestRow = Row;
		}
	}

	if (!BestRow)
	{
		return;
	}

	BloomMin = BestRow->MinBloom;
	BloomMax = FMath::Max(BestRow->MaxBloom, BloomMin);
	BloomPerShot = BestRow->IncPerShot;
	BloomRecoveryRate = BestRow->RecoveryRate;
	BloomCurrent = FMath::Clamp(BloomCurrent, BloomMin, BloomMax);
}

void ARangedWeaponBase::HandleAutoFire()
{
	if (!bIsAttacking || !Super::CanAttack() || bIsReloading || CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire stop Attack=%d BaseCanAttack=%d Reloading=%d Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), bIsAttacking ? 1 : 0, Super::CanAttack() ? 1 : 0, bIsReloading ? 1 : 0, CurrentAmmo);
		StopAttack();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] HandleAutoFire tick Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo);
	PerformAttack();
}

void ARangedWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARangedWeaponBase, CurrentAmmo);
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
	if (!HasAuthority())
	{
		return;
	}

	if (bIsAttacking)
	{
		UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack skipped: already attacking"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!CanAttack())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s [%s] StartAttack blocked CanAttack=false Ammo=%d Reloading=%d Cooldown=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, bIsReloading ? 1 : 0, bAttackOnCooldown ? 1 : 0);
		if (CurrentAmmo <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s [%s] No ammo"), OutlierNet::GetNetPrefix(this), *GetName());
		}
		return;
	}

	PerformAttack(); // 첫 발 즉시 발사

	UE_LOG(LogTemp, Log, TEXT("%s [%s] StartAttack Ammo=%d Automatic=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo, bIsAutomatic ? 1 : 0);
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
	if (!HasAuthority())
	{
		return;
	}

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

	UE_LOG(LogTemp, Log, TEXT("%s [%s] PerformAttack AmmoBefore=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo);
	RefreshBloomSettingsFromState();
	ConsumeAmmo();
	FireShot();

	ApplyBloomPerShot();
	ApplyRecoil();

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		Shooter->HandleFireShotAnimation();
	}

	StartAttackCooldown();
	StartReuseCooldown();

	if (CurrentAmmo == 0 && CanReload())
	{
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
		{
			Shooter->HandleAutoReloadRequested();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("%s [%s] Fire success Ammo=%d"), OutlierNet::GetNetPrefix(this), *GetName(), CurrentAmmo);
}
