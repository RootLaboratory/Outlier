// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "Shooter/ShooterFirstPersonAnimInstance.h"

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
	ForceNetUpdate();

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
	const FVector BaseDirection = CameraRotation.Vector().GetSafeNormal();
	const float ShotSpreadDegrees = FMath::Max(BloomCurrent, 0.0f);
	const FVector ShotDirection = ShotSpreadDegrees > KINDA_SMALL_NUMBER
		? FMath::VRandCone(BaseDirection, FMath::DegreesToRadians(ShotSpreadDegrees)).GetSafeNormal()
		: BaseDirection;
	LastShotBaseDirection = BaseDirection;
	LastShotDirection = ShotDirection;
	LastShotSpreadDegrees = ShotSpreadDegrees;
	bHasLastShotDirection = true;
	FVector End = Start + (CameraRotation.Vector() * EffectiveRange); // 사거리


	End = Start + (ShotDirection * EffectiveRange);

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

	//DrawDebugLine(
	//	GetWorld(),
	//	Start,
	//	End,
	//	FColor::Red,
	//	false,   // PersistentLines
	//	3.0f,    // LifeTime
	//	0,       // DepthPriority
	//	1.0f     // Thickness
	//);

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


	ClientNotifyShotFired(GetNormalizedLastShotDirection());

	{
		AActor* HitActor = Hit.GetActor();
		const FVector TraceEndPoint = bHit ? Hit.ImpactPoint : End;
		const FVector ImpactNormal = bHit ? Hit.ImpactNormal : -ShotDirection;
		MulticastPlayFireFX(TraceEndPoint, ImpactNormal, HitActor);

		if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
		{
			if (GunSound)
			{
				VisualSubsystem->PlaySoundAtLocation(GunSound, Start);
			}
		}
	}

	FColor LineColor = bHit ? FColor::Green : FColor::Red;

	/*DrawDebugLine(
		GetWorld(),
		Start,
		End,
		LineColor,
		false,
		3.0f,
		0,
		1.0f
	);*/
}

// 반동, 탄 퍼짐은 추후 작업 예정
void ARangedWeaponBase::ApplyRecoil()
{
	ApplyRecoilWithShotDirection(GetNormalizedLastShotDirection());
}

FVector2D ARangedWeaponBase::GetNormalizedLastShotDirection() const
{
	if (!bHasLastShotDirection)
	{
		return FVector2D::ZeroVector;
	}

	const FRotator BaseRotation = LastShotBaseDirection.Rotation();
	const FRotator ShotRotation = LastShotDirection.Rotation();
	const float NormalizationSpread = FMath::Max(LastShotSpreadDegrees, 0.01f);

	return FVector2D(
		FMath::Clamp(FMath::FindDeltaAngleDegrees(BaseRotation.Yaw, ShotRotation.Yaw) / NormalizationSpread, -1.0f, 1.0f),
		FMath::Clamp(FMath::FindDeltaAngleDegrees(BaseRotation.Pitch, ShotRotation.Pitch) / NormalizationSpread, -1.0f, 1.0f)
	);
}

void ARangedWeaponBase::ApplyRecoilWithShotDirection(const FVector2D& NormalizedShotDirection)
{
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	if (!Shooter || !Shooter->IsLocallyControlled())
	{
		return;
	}

	Shooter->AddWeaponCameraRecoil(
		RecoilPitchAmplitude * RecoilMultiplier,
		RecoilLocationXAmplitude * RecoilMultiplier,
		RecoilLocationYAmplitude * RecoilMultiplier,
		RecoilFovAmplitude * RecoilMultiplier,
		RecoilRecoverySpeed,
		NormalizedShotDirection
	);

	USkeletalMeshComponent* FirstPersonMesh = Shooter->GetFirstPersonMesh();
	if (!FirstPersonMesh)
	{
		return;
	}

	UShooterFirstPersonAnimInstance* FPAnim =
		Cast<UShooterFirstPersonAnimInstance>(FirstPersonMesh->GetAnimInstance());

	if (FPAnim)
	{
		FPAnim->AddViewModelRecoil(RecoilMultiplier, NormalizedShotDirection);
	}
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
	RefreshRecoilSettingsFromState();
}

void ARangedWeaponBase::ApplySightMesh()
{
	if (!SightMesh)
	{
		if (FirstSight && FirstSight->GetStaticMesh())
		{
			SightMesh = FirstSight->GetStaticMesh();
		}
		else if (ThirdSight && ThirdSight->GetStaticMesh())
		{
			SightMesh = ThirdSight->GetStaticMesh();
		}
	}

	if (FirstSight)
	{
		if (SightMesh)
		{
			FirstSight->SetStaticMesh(SightMesh);
		}
		FirstSight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FirstSight->SetCollisionResponseToAllChannels(ECR_Ignore);
		FirstSight->SetGenerateOverlapEvents(false);
		FirstSight->SetOnlyOwnerSee(true);
	}

	if (ThirdSight)
	{
		if (SightMesh)
		{
			ThirdSight->SetStaticMesh(SightMesh);
		}
		ThirdSight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ThirdSight->SetCollisionResponseToAllChannels(ECR_Ignore);
		ThirdSight->SetGenerateOverlapEvents(false);
		ThirdSight->SetOwnerNoSee(true);
	}

	if (ShadowSight)
	{
		if (SightMesh)
		{
			ShadowSight->SetStaticMesh(SightMesh);
		}
		ShadowSight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowSight->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShadowSight->SetGenerateOverlapEvents(false);
		ShadowSight->SetHiddenInGame(true);
		ShadowSight->SetVisibility(true, true);
		ShadowSight->SetRenderInMainPass(true);
		ShadowSight->SetRenderInDepthPass(false);
		ShadowSight->SetOwnerNoSee(false);
		ShadowSight->SetOnlyOwnerSee(false);
	}
}

void ARangedWeaponBase::ApplyMagazineMeshSettings()
{
	UStaticMeshComponent* MagazineComponents[] = { FirstHandMagazineMesh, ThirdHandMagazineMesh, ShadowHandMagazineMesh };
	for (UStaticMeshComponent* MagazineComponent : MagazineComponents)
	{
		if (!MagazineComponent)
		{
			continue;
		}

		if (MagazineMesh)
		{
			MagazineComponent->SetStaticMesh(MagazineMesh);
		}
		MagazineComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MagazineComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		MagazineComponent->SetGenerateOverlapEvents(false);
		MagazineComponent->SetHiddenInGame(true);
	}

	if (FirstHandMagazineMesh)
	{
		FirstHandMagazineMesh->SetOnlyOwnerSee(true);
	}
	if (ThirdHandMagazineMesh)
	{
		ThirdHandMagazineMesh->SetOwnerNoSee(true);
	}
	if (ShadowHandMagazineMesh)
	{
		ShadowHandMagazineMesh->SetOwnerNoSee(false);
		ShadowHandMagazineMesh->SetOnlyOwnerSee(false);
		ShadowHandMagazineMesh->SetRenderInMainPass(true);
		ShadowHandMagazineMesh->SetRenderInDepthPass(false);
	}
}

void ARangedWeaponBase::HideHandMagazine()
{
	if (FirstHandMagazineMesh)
	{
		FirstHandMagazineMesh->SetHiddenInGame(true);
	}
	if (ThirdHandMagazineMesh)
	{
		ThirdHandMagazineMesh->SetHiddenInGame(true);
	}
	if (ShadowHandMagazineMesh)
	{
		ShadowHandMagazineMesh->SetHiddenInGame(true);
		ShadowHandMagazineMesh->SetCastShadow(false);
		ShadowHandMagazineMesh->SetCastHiddenShadow(false);
	}
}

void ARangedWeaponBase::AttachMagazineToLeftHand(AShooterCharacter*)
{
	ApplyMagazineMeshSettings();

	if (FirstHandMagazineMesh)
	{
		FirstHandMagazineMesh->SetHiddenInGame(!FirstHandMagazineMesh->GetStaticMesh());
	}
	if (ThirdHandMagazineMesh)
	{
		ThirdHandMagazineMesh->SetHiddenInGame(!ThirdHandMagazineMesh->GetStaticMesh());
	}
	if (ShadowHandMagazineMesh)
	{
		const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
		const bool bLocalView = Shooter && Shooter->IsLocallyControlled();
		const bool bShowMagazineShadow = bLocalView && ShadowHandMagazineMesh->GetStaticMesh();
		ShadowHandMagazineMesh->SetHiddenInGame(true);
		ShadowHandMagazineMesh->SetVisibility(true, true);
		ShadowHandMagazineMesh->SetCastShadow(bShowMagazineShadow);
		ShadowHandMagazineMesh->SetCastHiddenShadow(bShowMagazineShadow);
	}
}

void ARangedWeaponBase::AttachMagazineToWeapon()
{
	HideHandMagazine();
}

void ARangedWeaponBase::AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner)
{
	Super::AttachWeaponMeshesToOwner(Weapon, NewOwner);

	ApplySightMesh();
	ApplyMagazineMeshSettings();

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(NewOwner);
	if (Shooter)
	{
		if (USkeletalMeshComponent* FirstPersonMesh = Shooter->GetFirstPersonMesh())
		{
			if (FirstHandMagazineMesh)
			{
				FirstHandMagazineMesh->AttachToComponent(
					FirstPersonMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					LeftHandMagazineSocketName
				);
			}
		}

		if (USkeletalMeshComponent* ThirdPersonMesh = Shooter->GetMesh())
		{
			if (ThirdHandMagazineMesh)
			{
				ThirdHandMagazineMesh->AttachToComponent(
					ThirdPersonMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					LeftHandMagazineSocketName
				);
			}
		}

		if (USkeletalMeshComponent* ShadowMesh = Shooter->GetShadowMesh())
		{
			if (ShadowHandMagazineMesh)
			{
				ShadowHandMagazineMesh->AttachToComponent(
					ShadowMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					LeftHandMagazineSocketName
				);
			}
		}
	}

	

	if (SightMesh)
	{
		if (USkeletalMeshComponent* FirstWeapon = Weapon->GetFirstPersonWeaponMesh())
		{
			if (FirstSight)
			{
				FirstSight->AttachToComponent(
					FirstWeapon,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					SightSocketName
				);
			}
		}

		if (USkeletalMeshComponent* ThirdWeapon = Weapon->GetThirdPersonWeaponMesh())
		{
			if (ThirdSight)
			{
				ThirdSight->AttachToComponent(
					ThirdWeapon,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					SightSocketName
				);
			}
		}

		if (USkeletalMeshComponent* ShadowWeapon = Weapon->GetShadowWeaponMesh())
		{
			if (ShadowSight)
			{
				ShadowSight->AttachToComponent(
					ShadowWeapon,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					SightSocketName
				);
			}
		}
	}

	RefreshShadowWeaponPresentation();

}


void ARangedWeaponBase::MulticastPlayFireFX_Implementation(FVector_NetQuantize TraceEnd, FVector_NetQuantizeNormal ImpactNormal, AActor* Hit)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		return;
	}

	if (OwnerCharacter->IsLocallyControlled())
	{
		PlayFirstPersonFireFX(TraceEnd, ImpactNormal, Hit);
		return;
	}

	PlayThirdPersonFireFX(TraceEnd, ImpactNormal, Hit);
}


void ARangedWeaponBase::OnEquipped(ACharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	if (FirstSight)
	{
		FirstSight->SetHiddenInGame(true);
	}
	if (ThirdSight)
	{
		ThirdSight->SetHiddenInGame(true);
	}
	if (ShadowSight)
	{
		ShadowSight->SetHiddenInGame(true);
		ShadowSight->SetCastShadow(false);
		ShadowSight->SetCastHiddenShadow(false);
	}
	if (FirstHandMagazineMesh)
	{
		FirstHandMagazineMesh->SetHiddenInGame(true);
	}
	if (ThirdHandMagazineMesh)
	{
		ThirdHandMagazineMesh->SetHiddenInGame(true);
	}
	if (ShadowHandMagazineMesh)
	{
		ShadowHandMagazineMesh->SetHiddenInGame(true);
		ShadowHandMagazineMesh->SetCastShadow(false);
		ShadowHandMagazineMesh->SetCastHiddenShadow(false);
	}
	UpdateLocalAmmoUI();
}

void ARangedWeaponBase::ShowEquippedPresentation()
{
	Super::ShowEquippedPresentation();
	ApplySightMesh();

	if (FirstSight)
	{
		FirstSight->SetHiddenInGame(!SightMesh);
	}
	if (ThirdSight)
	{
		ThirdSight->SetHiddenInGame(!SightMesh);
	}
	RefreshShadowWeaponPresentation();
	HideHandMagazine();
}

void ARangedWeaponBase::RefreshShadowWeaponPresentation()
{
	Super::RefreshShadowWeaponPresentation();

	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner);
	const bool bLocalView = Shooter && Shooter->IsLocallyControlled();

	if (ShadowSight)
	{
		if (SightMesh)
		{
			ShadowSight->SetStaticMesh(SightMesh);
		}
		const bool bShowSightShadow = bLocalView && SightMesh != nullptr && IsEquipped();
		ShadowSight->SetHiddenInGame(true);
		ShadowSight->SetVisibility(true, true);
		ShadowSight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowSight->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShadowSight->SetGenerateOverlapEvents(false);
		ShadowSight->SetRenderInMainPass(true);
		ShadowSight->SetRenderInDepthPass(false);
		ShadowSight->SetCastShadow(bShowSightShadow);
		ShadowSight->SetCastHiddenShadow(bShowSightShadow);
	}
	if (ThirdSight)
	{
		ThirdSight->SetCastShadow(!bLocalView);
		ThirdSight->SetCastHiddenShadow(false);
	}

	if (ShadowHandMagazineMesh)
	{
		if (MagazineMesh)
		{
			ShadowHandMagazineMesh->SetStaticMesh(MagazineMesh);
		}
		const bool bShowMagazineShadow =
			bLocalView &&
			MagazineMesh != nullptr &&
			IsEquipped() &&
			ThirdHandMagazineMesh &&
			!ThirdHandMagazineMesh->bHiddenInGame;
		ShadowHandMagazineMesh->SetHiddenInGame(true);
		ShadowHandMagazineMesh->SetVisibility(true, true);
		ShadowHandMagazineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowHandMagazineMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShadowHandMagazineMesh->SetGenerateOverlapEvents(false);
		ShadowHandMagazineMesh->SetRenderInMainPass(true);
		ShadowHandMagazineMesh->SetRenderInDepthPass(false);
		ShadowHandMagazineMesh->SetCastShadow(bShowMagazineShadow);
		ShadowHandMagazineMesh->SetCastHiddenShadow(bShowMagazineShadow);
	}
	if (ThirdHandMagazineMesh)
	{
		ThirdHandMagazineMesh->SetCastShadow(!bLocalView);
		ThirdHandMagazineMesh->SetCastHiddenShadow(false);
	}
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

void ARangedWeaponBase::ClientNotifyShotFired_Implementation(FVector2D NormalizedShotDirection)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
		{
			UISubsystem->OnRep_ShootCrosshairChanged(ReuseCooldown);
		}

		if (!HasAuthority())
		{
			ApplyRecoilWithShotDirection(NormalizedShotDirection);
		}
	}
}

void ARangedWeaponBase::PlayThirdPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit)
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
			const FRotator ImpactRotation = ImpactNormal.GetSafeNormal().Rotation();
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);
			bool bSpawnFallbackDecal = true;

			if (Provider)
			{
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				if (AssetSet.DecalDef && !AssetSet.DecalDef->DecalMaterial)
				{
					AssetSet.DecalDef = nullptr;
				}

				VisualSubsystem->FeaturesEffect(TraceEnd, ImpactRotation, AssetSet);
				bSpawnFallbackDecal = !AssetSet.DecalDef;
			}

			if (bSpawnFallbackDecal && WeaponDecal)
			{
				VisualSubsystem->SpawnMarkAtLocation(WeaponDecal, TraceEnd, ImpactRotation);
			}

		}

	}
}

void ARangedWeaponBase::PlayFirstPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit)
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
			const FRotator ImpactRotation = ImpactNormal.GetSafeNormal().Rotation();
			IVisualEffectProvider* Provider = Cast<IVisualEffectProvider>(Hit);
			bool bSpawnFallbackDecal = true;

			if (Provider)
			{
				FVisualEventSet AssetSet = Provider->GetVisualEventSet();
				if (AssetSet.DecalDef && !AssetSet.DecalDef->DecalMaterial)
				{
					AssetSet.DecalDef = nullptr;
				}

				VisualSubsystem->FeaturesEffect(TraceEnd, ImpactRotation, AssetSet);
				bSpawnFallbackDecal = !AssetSet.DecalDef;
			}

			if (bSpawnFallbackDecal && WeaponDecal)
			{
				VisualSubsystem->SpawnMarkAtLocation(WeaponDecal, TraceEnd, ImpactRotation);
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

ARangedWeaponBase::ARangedWeaponBase() : AWeaponBase()
{
	FirstSight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstSight"));
	FirstSight->SetupAttachment(FirstPersonWeaponMesh);
	ThirdSight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdSight"));
	ThirdSight->SetupAttachment(ThirdPersonWeaponMesh);
	ShadowSight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShadowSight"));
	ShadowSight->SetupAttachment(ShadowWeaponMesh);
	ShadowSight->SetHiddenInGame(true);

	FirstHandMagazineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstHandMagazine"));
	FirstHandMagazineMesh->SetupAttachment(FirstPersonWeaponMesh);
	FirstHandMagazineMesh->SetHiddenInGame(true);

	ThirdHandMagazineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdHandMagazine"));
	ThirdHandMagazineMesh->SetupAttachment(ThirdPersonWeaponMesh);
	ThirdHandMagazineMesh->SetHiddenInGame(true);

	ShadowHandMagazineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShadowHandMagazine"));
	ShadowHandMagazineMesh->SetupAttachment(ShadowWeaponMesh);
	ShadowHandMagazineMesh->SetHiddenInGame(true);
}

void ARangedWeaponBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplySightMesh();
	ApplyMagazineMeshSettings();
}

void ARangedWeaponBase::InitializeFromDataTables()
{
	Super::InitializeFromDataTables();

	if (const FWeaponCoreRow* CoreRow = WeaponCoreRow.GetRow<FWeaponCoreRow>(TEXT("InitializeRangedWeaponCore")))
	{
		MagazineSize = FMath::Max(CoreRow->MagazineSize, 0);
		CurrentAmmo = MagazineSize;
		BloomMin = CoreRow->DefaultMinBloom;
		BloomMax = FMath::Max(CoreRow->DefaultMaxBloom, BloomMin);
		BloomCurrent = FMath::Clamp(BloomCurrent, BloomMin, BloomMax);
		RecoilMultiplier = FMath::Max(CoreRow->GameplayRecoilMultiplier, 0.0f);
		bIsAutomatic = FireMode == EWeaponFireMode::FullAuto;
		BloomProfileId = CoreRow->BloomProfileId;
		ProjectileProfileId = CoreRow->ProjectileProfileId;
		RecoilProfileId = CoreRow->RecoilProfileId;
	}

	InitializeBloomFromDataTable();
	InitializeRecoilFromDataTable();
	InitializeProjectileFromDataTable();
	ApplyFeedbackDefinition();
	ApplySightMesh();
	ApplyMagazineMeshSettings();
}

void ARangedWeaponBase::InitializeBloomFromDataTable()
{
	RefreshBloomSettingsFromState();
}

void ARangedWeaponBase::InitializeRecoilFromDataTable()
{
	RefreshRecoilSettingsFromState();
}

void ARangedWeaponBase::RefreshRecoilSettingsFromState()
{
	const EWeaponAimMode DesiredAimMode = bIsAiming ? EWeaponAimMode::ADS : EWeaponAimMode::Hip;
	const FWeaponRecoilRow* RecoilRow = nullptr;

	if (WeaponRecoilTable && !RecoilProfileId.IsNone())
	{
		TArray<FWeaponRecoilRow*> RecoilRows;
		WeaponRecoilTable->GetAllRows<FWeaponRecoilRow>(TEXT("RefreshWeaponRecoil"), RecoilRows);

		for (const FWeaponRecoilRow* CandidateRow : RecoilRows)
		{
			if (!CandidateRow || CandidateRow->RecoilProfileId != RecoilProfileId)
			{
				continue;
			}

			if (CandidateRow->AimMode == DesiredAimMode)
			{
				RecoilRow = CandidateRow;
				break;
			}

			if (CandidateRow->AimMode == EWeaponAimMode::Any)
			{
				RecoilRow = CandidateRow;
			}
		}
	}

	if (!RecoilRow && WeaponRecoilTable && !RecoilProfileId.IsNone())
	{
		RecoilRow = WeaponRecoilTable->FindRow<FWeaponRecoilRow>(RecoilProfileId, TEXT("RefreshWeaponRecoil"));
	}

	if (!RecoilRow)
	{
		RecoilRow = RecoilDataRow.GetRow<FWeaponRecoilRow>(TEXT("RefreshWeaponRecoil"));
	}

	if (!RecoilRow)
	{
		return;
	}

	RecoilPitchAmplitude = RecoilRow->ControlPitchAmplitude;
	RecoilLocationXAmplitude = RecoilRow->CameraLocationXAmplitude;
	RecoilLocationYAmplitude = RecoilRow->CameraLocationYAmplitude;
	RecoilFovAmplitude = RecoilRow->CameraFovKickAmplitude;
	RecoilRecoverySpeed = RecoilRow->ControlRecoverySpeed;
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
