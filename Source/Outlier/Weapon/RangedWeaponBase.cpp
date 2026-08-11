// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/RangedWeaponBase.h"

#include "Damage/OutlierTaggedDamageEvent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
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
#include "Enemy/EnemyBase.h"
#include "OutlierNetUtils.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/WeaponCoreRow.h"
#include "Weapon/WeaponBloomRow.h"
#include "Weapon/WeaponFeedbackDefinition.h"
#include "Weapon/WeaponProjectileRow.h"
#include "Weapon/WeaponRecoilRow.h"
#include "FirstPerson/FirstPersonPlayerCameraManager.h"
#include "Shooter/ShooterAnimInstance.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Outlier.h"
#include "Room/RoomTagComponent.h"
#include "Interface/RoomTagInterface.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Enemy/SelfDestructDrone.h"
#include "Explosion/ExplosiveProp.h"
#include "Audio/OutlierAudioSubsystem.h"

namespace
{
	void SpawnAttachedMuzzleEffect(
		const UTrailEffectDefinition* Def,
		USceneComponent* AttachTarget,
		FName SocketName,
		const FVector& Location,
		const FRotator& Rotation)
	{
		if (!Def || !Def->FXAsset || !AttachTarget)
		{
			return;
		}

		const FVector FinalLocation = Location + Def->RelativeLocation;
		const FRotator FinalRotation = Rotation + Def->RelativeRotation + Def->RotationOffset;

		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(
				Niagara,
				AttachTarget,
				SocketName,
				FinalLocation,
				FinalRotation,
				Def->Scale,
				EAttachLocation::KeepWorldPosition,
				true,
				ENCPoolMethod::AutoRelease,
				true,
				true);
		}
		else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
		{
			UGameplayStatics::SpawnEmitterAttached(
				Particle,
				AttachTarget,
				SocketName,
				FinalLocation,
				FinalRotation,
				Def->Scale,
				EAttachLocation::KeepWorldPosition,
				true,
				EPSCPoolMethod::AutoRelease,
				true);
		}
	}
}

namespace
{
	constexpr float BloomRecoveryTickInterval = 0.05f;
	constexpr float ControlKickTimerInterval = 1.0f / 120.0f;
	constexpr float MaxControlKickDurationSeconds = 0.025f;
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

void ARangedWeaponBase::StartPostBurstCooldown()
{
	if (PostBurstCooldownSeconds <= 0.0f || !GetWorld())
	{
		return;
	}

	bOnPostBurstCooldown = true;
	GetWorld()->GetTimerManager().ClearTimer(PostBurstCooldownTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		PostBurstCooldownTimerHandle,
		this,
		&ARangedWeaponBase::FinishPostBurstCooldown,
		PostBurstCooldownSeconds,
		false
	);
}

void ARangedWeaponBase::FinishPostBurstCooldown()
{
	bOnPostBurstCooldown = false;
}

void ARangedWeaponBase::ForcePostBurstCooldown()
{
	if (!HasAuthority())
	{
		return;
	}
	StopAttack();
	StartPostBurstCooldown();
}

bool ARangedWeaponBase::CanReload() const
{
	return !bIsReloading
		&& !bInfiniteAmmo
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
	if (bInfiniteAmmo)
	{
		return;
	}

	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);
	UpdateLocalAmmoUI();
}


void ARangedWeaponBase::FireShot()
{
	FireShotFromMuzzle(NAME_None, true);
}

void ARangedWeaponBase::FireShotFromMuzzle(FName FiredMuzzleSocketName, bool bPlayShotSound)
{
	if (Cast<APartnerCharacter>(WeaponOwner))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponVFX][FireShot] Weapon=%s Authority=%d Owner=%s Controller=%s"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0,
			*GetNameSafe(WeaponOwner),
			*GetNameSafe(WeaponOwner ? WeaponOwner->GetInstigatorController() : nullptr));
	}

	if (!HasAuthority())
	{
		return;
	}

	if (!WeaponOwner)
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		return;
	}

	if (!OwnerCharacter->GetController())
	{
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
		AActor* HitActor = Hit.GetActor();
		const float HitDistance = FVector::Distance(Start, Hit.ImpactPoint);
		const float DamageToApply = GetDamageAtDistance(HitDistance);
		FHitResult ResolvedDamageHit = Hit;
		bool bIsCoreHit = false;

		if (AEnemyBase* HitEnemy = Cast<AEnemyBase>(HitActor))
		{
			// 어떤 걸 맞았는지(HitEnemy 확정)는 위의 단일 트레이스 결과 그대로 사용 — 벽/다른 액터에 대한
			// 판정은 절대 안 바뀜. CoreHitboxComponent가 Body(SkeletalMeshComponent)의 Physics Asset
			// 바디 안쪽에 겹쳐 있으면, 별도 컴포넌트로 분리했음에도 여전히 더 가까운 Body 바디에 가려져서
			// 안 잡히는 것으로 보임 — 그래서 이 2차 프로브에서는 아예 Mesh(Body/Gun이 붙어있는 컴포넌트)
			// 자체를 무시해서, Core가 Body 안쪽 어디에 있든 상관없이 확실히 잡히게 함


			if (HitEnemy && HitEnemy->HasCoreWeakPoint())
			{
				constexpr float CoreProbeExtraDistance = 1000.0f;
				const FVector ProbeEnd = Start + (ShotDirection * (HitDistance + CoreProbeExtraDistance));

				FCollisionQueryParams CoreProbeParams = Params;
				CoreProbeParams.AddIgnoredComponent(HitEnemy->GetMesh());

				TArray<FHitResult> BoneHitResults;
				GetWorld()->LineTraceMultiByChannel(BoneHitResults, Start, ProbeEnd, ECC_PhysicsBody, CoreProbeParams);

				float BestWeakPointMultiplier = HitEnemy->GetWeakPointDamageMultiplier(Hit.GetComponent());
				for (const FHitResult& BodyHit : BoneHitResults)
				{
					if (BodyHit.GetActor() == HitEnemy)
					{
						const float CandidateMultiplier = HitEnemy->GetWeakPointDamageMultiplier(BodyHit.GetComponent());
						if (CandidateMultiplier > BestWeakPointMultiplier)
						{
							BestWeakPointMultiplier = CandidateMultiplier;
							ResolvedDamageHit = BodyHit;
						}
					}
				}

				bIsCoreHit = BestWeakPointMultiplier > 1.0f;
			}
			
			

		}

		/*else if (AExplosiveProp* HitExplosive = Cast<AExplosiveProp>(HitActor))
		{
			bIsCoreHit =
				HitExplosive->IsMountedOnSelfDestructDrone()
				&& Cast<ASelfDestructDrone>(HitExplosive->GetOwner()) != nullptr;

			UE_LOG(LogTemp, Error, TEXT("HitExplosive hit called"));

		}*/

		if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
		{
			UISubsystem->OnRep_AttackSign(bIsCoreHit ? EAttackSign::Critical : EAttackSign::Default);
		}
		

		FOutlierTaggedDamageEvent DamageEvent;
		DamageEvent.DamageTag = OutlierGameplayTags::Damage::Weapon();
		DamageEvent.HitResult = ResolvedDamageHit;
		DamageEvent.DamageOrigin = Start;

		if (HitActor)
		{
			HitActor->TakeDamage(DamageToApply, DamageEvent, OwnerCharacter->GetController(), this);
		}
	}
	{
		AActor* HitActor = Hit.GetActor();
		const FVector TraceEndPoint = bHit ? Hit.ImpactPoint : End;
		const FVector ImpactNormal = bHit ? Hit.ImpactNormal : -ShotDirection;
		MulticastPlayFireFX(
			TraceEndPoint,
			ImpactNormal,
			HitActor,
			GetNormalizedLastShotDirection(),
			FiredMuzzleSocketName);

		if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
		{
			if (bPlayShotSound && GunSound)
			{
				VisualSubsystem->PlaySoundAtLocation(GunSound, Start);
			}
		}
	}

	if (bReportArenaWideNoise)
	{
		if (AEnemyBase* Enemy = Cast<AEnemyBase>(OwnerCharacter))
		{
			if (Enemy->IsEnemyPossessed())
			{
				ReportArenaWideNoise(OwnerCharacter);
			}
		}
		else
		{
			// Shooter 라이플
			ReportArenaWideNoise(OwnerCharacter);
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

// 서버에서 연사 누적 반동을 확정하고 소유 플레이어에게 짧은 화면 피드백을 전달한다.
void ARangedWeaponBase::ApplyRecoil()
{
	LastCalculatedControlRecoil = FVector2D::ZeroVector;
	LastWeaponCameraShakeScale = 0.0f;
	LastWeaponCameraShakeDuration = 0.0f;

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!HasAuthority()
		|| !OwnerCharacter
		|| !OwnerCharacter->IsPlayerControlled()
		|| !bHasActiveRecoilProfile
		|| RecoilMultiplier <= 0.0f)
	{
		return;
	}

	++RecoilShotSequence;
	LastCalculatedControlRecoil = OutlierWeaponRecoil::CalculateControlRecoil(
		ActiveRecoilProfile,
		RecoilMultiplier,
		RecoilShotSequence,
		RecoilRuntimeState);
	LastWeaponCameraShakeScale = FMath::Max(ActiveRecoilProfile.CameraShakeScale, 0.0f);
	LastWeaponCameraShakeDuration = FMath::Max(ActiveRecoilProfile.CameraShakeDuration, 0.0f);

	const float ResetDelay = FMath::Max(ActiveRecoilProfile.RecoilResetDelay, 0.0f);
	if (ResetDelay <= 0.0f)
	{
		ResetRecoilRuntimeState();
	}
	else if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			RecoilResetTimerHandle,
			this,
			&ARangedWeaponBase::ResetRecoilRuntimeState,
			ResetDelay,
			false);
	}

	if (OwnerCharacter->IsLocallyControlled())
	{
		ApplyLocalRecoilPresentation(
			GetNormalizedLastShotDirection(),
			LastCalculatedControlRecoil,
			ActiveRecoilProfile.ControlKickInterpSpeed,
			LastWeaponCameraShakeScale,
			LastWeaponCameraShakeDuration);
	}
	else
	{
		// 원격 소유자의 다음 서버 발사도 반동 방향을 사용하도록 권한 ControlRotation을 먼저 갱신한다.
		ApplyAuthoritativeControlRecoil(LastCalculatedControlRecoil);
	}
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

void ARangedWeaponBase::ApplyLocalRecoilPresentation(
	const FVector2D& NormalizedShotDirection,
	const FVector2D& ControlRecoilDelta,
	float ControlKickInterpSpeed,
	float CameraShakeScale,
	float CameraShakeDuration)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	QueueLocalControlRecoil(ControlRecoilDelta, ControlKickInterpSpeed);

	if (APlayerController* PlayerController = Cast<APlayerController>(OwnerCharacter->GetController()))
	{
		if (AFirstPersonPlayerCameraManager* CameraManager =
			Cast<AFirstPersonPlayerCameraManager>(PlayerController->PlayerCameraManager))
		{
			CameraManager->PlayWeaponCameraShake(
				CameraShakeScale,
				CameraShakeDuration,
				OwnerCharacter);
		}
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter);
	if (!Shooter)
	{
		return;
	}

	USkeletalMeshComponent* FirstPersonMesh = Shooter->GetFirstPersonMesh();
	if (!FirstPersonMesh)
	{
		return;
	}

	UShooterFirstPersonAnimInstance* FPAnim =
		Cast<UShooterFirstPersonAnimInstance>(FirstPersonMesh->GetAnimInstance());

	if (FPAnim)
	{
		FPAnim->AddViewModelRecoil(
			RecoilMultiplier * GetFirstPersonProceduralRecoilMultiplier(),
			NormalizedShotDirection
		);
	}
}

void ARangedWeaponBase::ApplyAuthoritativeControlRecoil(const FVector2D& ControlRecoilDelta)
{
	ApplyControlRotationDelta(ControlRecoilDelta);
}

void ARangedWeaponBase::QueueLocalControlRecoil(
	const FVector2D& ControlRecoilDelta,
	float ControlKickInterpSpeed)
{
	if (ControlRecoilDelta.IsNearlyZero())
	{
		return;
	}

	PendingLocalControlRecoil += ControlRecoilDelta;
	LocalControlKickInterpSpeed = FMath::Max(ControlKickInterpSpeed, 1.0f);
	LocalControlKickElapsedTime = 0.0f;

	// 첫 프레임에 대부분을 반영하고 남은 값만 짧은 활성 Timer로 마무리한다.
	HandleLocalControlKick();
	if (!PendingLocalControlRecoil.IsNearlyZero() && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			LocalControlKickTimerHandle,
			this,
			&ARangedWeaponBase::HandleLocalControlKick,
			ControlKickTimerInterval,
			true);
	}
}

void ARangedWeaponBase::HandleLocalControlKick()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || PendingLocalControlRecoil.IsNearlyZero())
	{
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(LocalControlKickTimerHandle);
		}
		PendingLocalControlRecoil = FVector2D::ZeroVector;
		return;
	}

	LocalControlKickElapsedTime += ControlKickTimerInterval;
	const float StepAlpha = 1.0f - FMath::Exp(-LocalControlKickInterpSpeed * ControlKickTimerInterval);
	FVector2D AppliedDelta = PendingLocalControlRecoil * StepAlpha;
	if (LocalControlKickElapsedTime >= MaxControlKickDurationSeconds
		|| PendingLocalControlRecoil.SizeSquared() <= FMath::Square(0.001f))
	{
		AppliedDelta = PendingLocalControlRecoil;
	}

	ApplyControlRotationDelta(AppliedDelta);
	PendingLocalControlRecoil -= AppliedDelta;

	if (PendingLocalControlRecoil.IsNearlyZero(0.001f) && GetWorld())
	{
		PendingLocalControlRecoil = FVector2D::ZeroVector;
		GetWorld()->GetTimerManager().ClearTimer(LocalControlKickTimerHandle);
	}
}

void ARangedWeaponBase::ApplyControlRotationDelta(const FVector2D& ControlRecoilDelta) const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	AController* OwnerController = OwnerCharacter ? OwnerCharacter->GetController() : nullptr;
	if (!OwnerController || ControlRecoilDelta.IsNearlyZero())
	{
		return;
	}

	FRotator ControlRotation = OwnerController->GetControlRotation();
	ControlRotation.Pitch = FMath::ClampAngle(
		ControlRotation.Pitch + ControlRecoilDelta.X,
		-89.0f,
		89.0f);
	ControlRotation.Yaw = FRotator::NormalizeAxis(ControlRotation.Yaw + ControlRecoilDelta.Y);
	OwnerController->SetControlRotation(ControlRotation);
}

void ARangedWeaponBase::ResetRecoilRuntimeState()
{
	RecoilRuntimeState.Reset();
}

void ARangedWeaponBase::CancelLocalRecoilPresentation()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LocalControlKickTimerHandle);
	}
	PendingLocalControlRecoil = FVector2D::ZeroVector;
	LocalControlKickElapsedTime = 0.0f;

	const ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	const APlayerController* PlayerController = OwnerCharacter
		? Cast<APlayerController>(OwnerCharacter->GetController())
		: nullptr;
	if (PlayerController)
	{
		if (AFirstPersonPlayerCameraManager* CameraManager =
			Cast<AFirstPersonPlayerCameraManager>(PlayerController->PlayerCameraManager))
		{
			CameraManager->StopWeaponCameraShake(true);
		}
	}
}

void ARangedWeaponBase::ApplyBloomPerShot()
{
	BloomCurrent = FMath::Clamp(BloomCurrent + BloomPerShot, BloomMin, BloomMax);
	EnsureBloomRecoveryTimer();
}

void ARangedWeaponBase::RecoverBloom(float DeltaTime)
{
	BloomCurrent = FMath::FInterpConstantTo(BloomCurrent, BloomMin, DeltaTime, BloomRecoveryRate);
}

void ARangedWeaponBase::EnsureBloomRecoveryTimer()
{
	if (!HasAuthority()
		|| !GetWorld()
		|| BloomRecoveryRate <= 0.0f
		|| BloomCurrent <= BloomMin + KINDA_SMALL_NUMBER
		|| GetWorld()->GetTimerManager().IsTimerActive(BloomRecoveryTimerHandle))
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		BloomRecoveryTimerHandle,
		this,
		&ARangedWeaponBase::HandleBloomRecoveryTimer,
		BloomRecoveryTickInterval,
		true);
}

void ARangedWeaponBase::HandleBloomRecoveryTimer()
{
	if (!GetWorld())
	{
		return;
	}

	if (bIsAttacking)
	{
		return;
	}

	RecoverBloom(BloomRecoveryTickInterval);
	if (BloomCurrent <= BloomMin + KINDA_SMALL_NUMBER)
	{
		BloomCurrent = BloomMin;
		GetWorld()->GetTimerManager().ClearTimer(BloomRecoveryTimerHandle);
	}
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
	EnsureBloomRecoveryTimer();
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

		FirstSight->SetRenderCustomDepth(true);
		FirstSight->SetCustomDepthStencilValue(3);
		FirstSight->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
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

UStaticMeshComponent* ARangedWeaponBase::GetFirstSightMesh() const
{
	if (FirstSight)
	{
		return FirstSight;
	}

	return nullptr;
}

void ARangedWeaponBase::AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner)
{
	Super::AttachWeaponMeshesToOwner(Weapon, NewOwner);

	ApplySightMesh();
	CacheSightAimMaterials();
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


void ARangedWeaponBase::MulticastPlayFireFX_Implementation(
	FVector_NetQuantize TraceEnd,
	FVector_NetQuantizeNormal ImpactNormal,
	AActor* Hit,
	FVector2D NormalizedShotDirection,
	FName FiredMuzzleSocketName)
{
	const bool bPartnerWeapon = Cast<APartnerCharacter>(WeaponOwner) != nullptr;
	if (bPartnerWeapon)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponVFX][Multicast] Weapon=%s Owner=%s TraceEnd=%s MuzzleDef=%s TrailDef=%s"),
			*GetNameSafe(this),
			*GetNameSafe(WeaponOwner),
			*TraceEnd.ToString(),
			*GetNameSafe(WeaponMuzzle),
			*GetNameSafe(WeaponTrail));
	}

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		if (USkeletalMeshComponent* ThirdPersonMesh = Shooter->GetMesh())
		{
			if (UShooterAnimInstance* TPAnim = Cast<UShooterAnimInstance>(ThirdPersonMesh->GetAnimInstance()))
			{
				TPAnim->AddThirdPersonRecoil(
					RecoilMultiplier * GetThirdPersonProceduralRecoilMultiplier(),
					NormalizedShotDirection
				);
			}
		}
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(WeaponOwner);
	if (!OwnerCharacter)
	{
		return;
	}

	const bool bUseFirstPersonPresentation =
		OwnerCharacter->IsPlayerControlled() && OwnerCharacter->IsLocallyControlled();
	if (bPartnerWeapon)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponVFX][Presentation] FirstPerson=%d PlayerControlled=%d LocallyControlled=%d"),
			bUseFirstPersonPresentation ? 1 : 0,
			OwnerCharacter->IsPlayerControlled() ? 1 : 0,
			OwnerCharacter->IsLocallyControlled() ? 1 : 0);
	}

	if (bUseFirstPersonPresentation)
	{
		PlayFirstPersonFireFX(TraceEnd, ImpactNormal, Hit, FiredMuzzleSocketName);
		return;
	}

	PlayThirdPersonFireFX(TraceEnd, ImpactNormal, Hit, FiredMuzzleSocketName);
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
	CacheSightAimMaterials();

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

int32 ARangedWeaponBase::ResolveADSBlurStencil()
{
	constexpr int32 DefaultADSWeaponStencil = 3;

	if (!FirstPersonWeaponMesh)
	{
		return DefaultADSWeaponStencil;
	}

	const int32 StencilValue = FirstPersonWeaponMesh->CustomDepthStencilValue;
	return StencilValue > 0 ? StencilValue : DefaultADSWeaponStencil;
}

void ARangedWeaponBase::ClientNotifyShotFired_Implementation(
	FVector2D NormalizedShotDirection,
	FVector2D ControlRecoilDelta,
	float ControlKickInterpSpeed,
	float CameraShakeScale,
	float CameraShakeDuration)
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
			ApplyLocalRecoilPresentation(
				NormalizedShotDirection,
				ControlRecoilDelta,
				ControlKickInterpSpeed,
				CameraShakeScale,
				CameraShakeDuration);
		}
	}
}

void ARangedWeaponBase::PlayThirdPersonFireFX(
	FVector TraceEnd, FVector ImpactNormal, AActor* Hit, FName FiredMuzzleSocketName)
{
	TArray<FTransform> MuzzleTransforms;
	ResolveMuzzleTransforms(false, FiredMuzzleSocketName, MuzzleTransforms);
	if (MuzzleTransforms.IsEmpty())
	{
		if (Cast<APartnerCharacter>(WeaponOwner))
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][TP] Failed: no valid muzzle transform."));
		}
		return;
	}

	if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
	{
		for (const FTransform& MuzzleTransform : MuzzleTransforms)
		{
			const FVector Start = MuzzleTransform.GetLocation();
			const FRotator MuzzleRotation = MuzzleTransform.Rotator();

			if (WeaponMuzzle)
			{
				if (APartnerCharacter* Partner = Cast<APartnerCharacter>(WeaponOwner))
				{
					SpawnAttachedMuzzleEffect(
						WeaponMuzzle,
						Partner->GetWeaponMuzzleComponent(false),
						Partner->GetWeaponMuzzleSocketName(false),
						Start,
						MuzzleRotation);
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[PartnerWeaponVFX][TP][Muzzle] Attached spawn requested. Definition=%s Location=%s Rotation=%s"),
						*GetNameSafe(WeaponMuzzle),
						*Start.ToString(),
						*MuzzleRotation.ToString());
				}
				else
				{
					VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);
				}
			}
			else if (Cast<APartnerCharacter>(WeaponOwner))
			{
				UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][TP][Muzzle] Failed: WeaponMuzzle definition is null."));
			}


			if (WeaponTrail)
			{
				VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, TraceEnd);
			}
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
	else if (Cast<APartnerCharacter>(WeaponOwner))
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][TP] Failed: VisualEventSubsystem is null."));
	}
}

void ARangedWeaponBase::PlayFirstPersonFireFX(
	FVector TraceEnd, FVector ImpactNormal, AActor* Hit, FName FiredMuzzleSocketName)
{
	TArray<FTransform> MuzzleTransforms;
	ResolveMuzzleTransforms(true, FiredMuzzleSocketName, MuzzleTransforms);
	if (MuzzleTransforms.IsEmpty())
	{
		if (Cast<APartnerCharacter>(WeaponOwner))
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][FP] Failed: no valid muzzle transform."));
		}
		return;
	}

	if (UVisualEventSubsystem* VisualSubsystem = GetWorld()->GetSubsystem<UVisualEventSubsystem>())
	{
		for (const FTransform& MuzzleTransform : MuzzleTransforms)
		{
			const bool bPartnerWeapon = Cast<APartnerCharacter>(WeaponOwner) != nullptr;
			const FVector Start = bPartnerWeapon
				? MuzzleTransform.GetLocation()
				: MuzzleTransform.GetLocation() + MuzzleTransform.GetRotation().GetForwardVector() * 10.0f;
			const FRotator MuzzleRotation = MuzzleTransform.Rotator();

			if (WeaponMuzzle)
			{
				if (APartnerCharacter* Partner = Cast<APartnerCharacter>(WeaponOwner))
				{
					SpawnAttachedMuzzleEffect(
						WeaponMuzzle,
						Partner->GetWeaponMuzzleComponent(true),
						Partner->GetWeaponMuzzleSocketName(true),
						Start,
						MuzzleRotation);
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[PartnerWeaponVFX][FP][Muzzle] Attached spawn requested. Definition=%s Location=%s Rotation=%s"),
						*GetNameSafe(WeaponMuzzle),
						*Start.ToString(),
						*MuzzleRotation.ToString());
				}
				else
				{
					VisualSubsystem->SpawnMuzzleEffect(WeaponMuzzle, Start, MuzzleRotation);
				}
			}
			else if (Cast<APartnerCharacter>(WeaponOwner))
			{
				UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][FP][Muzzle] Failed: WeaponMuzzle definition is null."));
			}

			if (WeaponTrail)
			{
				VisualSubsystem->SpawnBeamTrail(WeaponTrail, Start, TraceEnd);
			}
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
	else if (Cast<APartnerCharacter>(WeaponOwner))
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][FP] Failed: VisualEventSubsystem is null."));
	}
}

void ARangedWeaponBase::ResolveMuzzleTransforms(
	bool bFirstPerson, FName FiredMuzzleSocketName, TArray<FTransform>& OutMuzzleTransforms) const
{
	USkeletalMeshComponent* MuzzleComponent = nullptr;
	TArray<FName> SocketNames;

	// 일체형 무기는 Owner 본체의 소켓을 우선 사용한다.
	if (const IWeaponMuzzleProvider* MuzzleProvider = Cast<IWeaponMuzzleProvider>(WeaponOwner))
	{
		MuzzleComponent = MuzzleProvider->GetWeaponMuzzleComponent(bFirstPerson);
		if (!FiredMuzzleSocketName.IsNone())
		{
			SocketNames.Add(FiredMuzzleSocketName);
		}
		else
		{
			MuzzleProvider->GetWeaponMuzzleSocketNames(bFirstPerson, SocketNames);
		}
	}

	// Shooter처럼 독립 무기 메시를 쓰는 기존 무기는 원래 경로를 유지한다.
	if (!MuzzleComponent || SocketNames.IsEmpty())
	{
		MuzzleComponent = bFirstPerson ? FirstPersonWeaponMesh.Get() : ThirdPersonWeaponMesh.Get();
		SocketNames = { MuzzleSocketName };
	}

	if (!MuzzleComponent)
	{
		if (Cast<APartnerCharacter>(WeaponOwner))
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerWeaponVFX][Resolve] Failed: muzzle component is null. FirstPerson=%d"), bFirstPerson ? 1 : 0);
		}
		return;
	}

	for (const FName SocketName : SocketNames)
	{
		const bool bSocketExists = !SocketName.IsNone() && MuzzleComponent->DoesSocketExist(SocketName);
		if (Cast<APartnerCharacter>(WeaponOwner))
		{
			const FVector SocketLocation = bSocketExists
				? MuzzleComponent->GetSocketLocation(SocketName)
				: FVector::ZeroVector;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerWeaponVFX][Resolve] FirstPerson=%d Component=%s Socket=%s Exists=%d SocketLocation=%s"),
				bFirstPerson ? 1 : 0,
				*GetNameSafe(MuzzleComponent),
				*SocketName.ToString(),
				bSocketExists ? 1 : 0,
				*SocketLocation.ToString());
		}

		if (bSocketExists)
		{
			OutMuzzleTransforms.Add(MuzzleComponent->GetSocketTransform(SocketName, RTS_World));
		}
	}
}

ULocalPlayerUISubSystem* ARangedWeaponBase::GetLocalUISubsystem() const
{
	AFirstPersonCharacter* PlayerCharacter = Cast<AFirstPersonCharacter>(WeaponOwner);

	if (PlayerCharacter)
	{
		AShooterPlayerController* Player = Cast<AShooterPlayerController>(PlayerCharacter->GetController());
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

void ARangedWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelLocalRecoilPresentation();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RecoilResetTimerHandle);
	}
	ResetRecoilRuntimeState();

	Super::EndPlay(EndPlayReason);
}

void ARangedWeaponBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplySightMesh();
	ApplyMagazineMeshSettings();
	CacheSightAimMaterials();
}

void ARangedWeaponBase::CacheSightAimMaterials()
{
	SightAimMIDs.Reset();

	if (!FirstSight)
	{
		return;
	}

	//hard coding; 
	static constexpr int32 SightSlots[] = { 1, 2 };

	for (const int32 SlotIndex : SightSlots)
	{
		if (SlotIndex >= FirstSight->GetNumMaterials())
		{
			continue;
		}

		UMaterialInstanceDynamic* MID = FirstSight->CreateAndSetMaterialInstanceDynamic(SlotIndex);
		if (MID)
		{
			SightAimMIDs.Add(MID);
		}
	}
}

void ARangedWeaponBase::ReportArenaWideNoise(ACharacter* OwnerCharacter)
{
	if (!HasAuthority() || !OwnerCharacter)
	{
		return;
	}

	if (!bReportArenaWideNoise)
	{
		return;
	}

	const AOutlierPlayerState* PlayerState = OwnerCharacter->GetPlayerState<AOutlierPlayerState>();
	if (!PlayerState)
	{
		return;
	}

	const IRoomTagInterface* RoomTagOwner = Cast<IRoomTagInterface>(OwnerCharacter);
	if(!RoomTagOwner)
	{
		return;
	}

	const FGameplayTag CurrentRoomTag = RoomTagOwner->GetCurrentRoomTag();

	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		RoomSubsystem->NotifyRoomCombat(
			PlayerState->GetArenaId(),
			CurrentRoomTag,
			OwnerCharacter->GetActorLocation(),
			Cast<AEnemyBase>(OwnerCharacter)
		);
	}
}

void ARangedWeaponBase::SetSightAimMaterialFlag(bool bAiming)
{
	if (SightAimMIDs.Num() == 0)
	{
		CacheSightAimMaterials();
	}

	const float FlagValue = bAiming ? 1.0f : 0.0f;

	for (UMaterialInstanceDynamic* MID : SightAimMIDs)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(SightAimScalarParamName, FlagValue);
		}
	}
}

void ARangedWeaponBase::InitializeFromDataTables()
{
	Super::InitializeFromDataTables();

	if (const FWeaponCoreRow* CoreRow = WeaponCoreRow.GetRow<FWeaponCoreRow>(TEXT("InitializeRangedWeaponCore")))
	{
		MagazineSize = FMath::Max(CoreRow->MagazineSize, 0);
		CurrentAmmo = MagazineSize;
		bInfiniteAmmo = CoreRow->bInfiniteAmmo;
		BurstShotCount = FMath::Max(CoreRow->BurstShotCount, 0);
		PostBurstCooldownSeconds = FMath::Max(CoreRow->PostBurstCooldownSeconds, 0.0f);
		BloomMin = CoreRow->DefaultMinBloom;
		BloomMax = FMath::Max(CoreRow->DefaultMaxBloom, BloomMin);
		BloomCurrent = FMath::Clamp(BloomCurrent, BloomMin, BloomMax);
		RecoilMultiplier = FMath::Max(CoreRow->GameplayRecoilMultiplier, 0.0f);
		bIsAutomatic = FireMode == EWeaponFireMode::FullAuto;
		BloomProfileId = CoreRow->BloomProfileId;
		ProjectileProfileId = CoreRow->ProjectileProfileId;
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
	CacheRecoilProfiles();
	RefreshRecoilSettingsFromState();
}

void ARangedWeaponBase::CacheRecoilProfiles()
{
	bHasCachedHipRecoilProfile = false;
	bHasCachedADSRecoilProfile = false;
	bHasCachedAnyRecoilProfile = false;

	if (const FWeaponRecoilRow* HipRow =
		HipRecoilDataRow.GetRow<FWeaponRecoilRow>(TEXT("CacheHipWeaponRecoil")))
	{
		CachedHipRecoilProfile = *HipRow;
		bHasCachedHipRecoilProfile = true;
	}

	if (const FWeaponRecoilRow* ADSRow =
		ADSRecoilDataRow.GetRow<FWeaponRecoilRow>(TEXT("CacheADSWeaponRecoil")))
	{
		CachedADSRecoilProfile = *ADSRow;
		bHasCachedADSRecoilProfile = true;
	}

	if (const FWeaponRecoilRow* DefaultRow =
		DefaultRecoilDataRow.GetRow<FWeaponRecoilRow>(TEXT("CacheDefaultWeaponRecoil")))
	{
		CachedAnyRecoilProfile = *DefaultRow;
		bHasCachedAnyRecoilProfile = true;
	}
}

void ARangedWeaponBase::RefreshRecoilSettingsFromState()
{
	const FWeaponRecoilRow* RecoilRow = nullptr;
	if (bIsAiming && bHasCachedADSRecoilProfile)
	{
		RecoilRow = &CachedADSRecoilProfile;
	}
	else if (!bIsAiming && bHasCachedHipRecoilProfile)
	{
		RecoilRow = &CachedHipRecoilProfile;
	}
	else if (bHasCachedAnyRecoilProfile)
	{
		RecoilRow = &CachedAnyRecoilProfile;
	}

	bHasActiveRecoilProfile = RecoilRow != nullptr;
	ActiveRecoilProfile = RecoilRow ? *RecoilRow : FWeaponRecoilRow();

	// 조준 모드가 바뀌어도 이미 진행 중인 연사 누적 상태와 Reset Timer는 유지한다.
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
	if (!bIsAttacking || !Super::CanAttack() || bIsReloading || bOnPostBurstCooldown || (!bInfiniteAmmo && CurrentAmmo <= 0))
	{
		StopAttack();
		return;
	}

	PerformAttack();
}

float ARangedWeaponBase::GetAutomaticFireInterval() const
{
	// AttackInterval은 개별 탄환 간격이다. 두 총구가 동시에 발사되면 다음 그룹까지 두 탄환 분량을 기다린다.
	return FMath::Max(AttackInterval * FMath::Max(LastAttackMuzzleShotCount, 1), KINDA_SMALL_NUMBER);
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
		&& !bOnPostBurstCooldown
		&& !bIsReloading
		&& (bInfiniteAmmo || CurrentAmmo > 0);
}

void ARangedWeaponBase::StartAttack()
{
	if (Cast<APartnerCharacter>(WeaponOwner))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerWeaponVFX][StartAttack] Weapon=%s Authority=%d CanAttack=%d IsAttacking=%d Ammo=%d InfiniteAmmo=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0,
			CanAttack() ? 1 : 0,
			bIsAttacking ? 1 : 0,
			CurrentAmmo,
			bInfiniteAmmo ? 1 : 0);
	}

	if (!HasAuthority())
	{
		return;
	}

	if (bIsAttacking)
	{
		return;
	}

	if (!CanAttack())
	{
		return;
	}

	if (IWeaponMuzzleProvider* MuzzleProvider = Cast<IWeaponMuzzleProvider>(WeaponOwner);
		MuzzleProvider && MuzzleProvider->UsesIndependentMuzzleShots())
	{
		MuzzleProvider->ResetWeaponMuzzleSequence();
	}

	CurrentBurstShotCount = 0;
	LastAttackMuzzleShotCount = 1;
	bIsAttacking = true;
	PerformAttack(); // 첫 발 즉시 발사

	if (!bIsAttacking || !Super::CanAttack() || bIsReloading || bOnPostBurstCooldown || (!bInfiniteAmmo && CurrentAmmo <= 0))
	{
		return;
	}

	// Attack state is set by ranged fire flow.

	if (bIsAutomatic)
	{
		GetWorld()->GetTimerManager().SetTimer(
			AutoFireTimerHandle,
			this,
			&ARangedWeaponBase::HandleAutoFire,
			GetAutomaticFireInterval(),
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
	CurrentBurstShotCount = 0;
}

void ARangedWeaponBase::PerformAttack()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!Super::CanAttack() || bIsReloading || bOnPostBurstCooldown || (!bInfiniteAmmo && CurrentAmmo <= 0))
	{
		if (!bInfiniteAmmo && CurrentAmmo <= 0)
		{
			StopAttack();
		}
		return;
	}

	RefreshBloomSettingsFromState();

	IWeaponMuzzleProvider* MuzzleProvider = Cast<IWeaponMuzzleProvider>(WeaponOwner);
	const bool bIndependentMuzzleShots =
		MuzzleProvider && MuzzleProvider->UsesIndependentMuzzleShots();
	TArray<FName> MuzzleSockets;
	if (bIndependentMuzzleShots)
	{
		MuzzleProvider->GetWeaponMuzzleSocketNames(false, MuzzleSockets);
	}
	if (MuzzleSockets.IsEmpty())
	{
		if (bIndependentMuzzleShots)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[WeaponMuzzleGroup] No socket matched the active group. Weapon=%s Owner=%s Group=%s"),
				*GetNameSafe(this),
				*GetNameSafe(WeaponOwner),
				*MuzzleProvider->GetWeaponMuzzleSocketName(false).ToString());
			StopAttack();
			return;
		}
		MuzzleSockets.Add(NAME_None);
	}

	LastAttackMuzzleShotCount = 0;
	for (const FName MuzzleSocket : MuzzleSockets)
	{
		if ((BurstShotCount > 0 && CurrentBurstShotCount >= BurstShotCount)
			|| (!bInfiniteAmmo && CurrentAmmo <= 0))
		{
			break;
		}

		ConsumeAmmo();
		FireShotFromMuzzle(MuzzleSocket, LastAttackMuzzleShotCount == 0);
		++CurrentBurstShotCount;
		++LastAttackMuzzleShotCount;
	}

	if (LastAttackMuzzleShotCount <= 0)
	{
		StopAttack();
		return;
	}

	if (bIndependentMuzzleShots)
	{
		MuzzleProvider->AdvanceWeaponMuzzleSequence();
	}

	// 같은 그룹의 총구는 동일한 Bloom 값으로 방향을 계산하고, 발사된 탄환 수만큼 다음 Bloom을 누적한다.
	for (int32 ShotIndex = 0; ShotIndex < LastAttackMuzzleShotCount; ++ShotIndex)
	{
		ApplyBloomPerShot();
	}
	ApplyRecoil();
	ClientNotifyShotFired(
		GetNormalizedLastShotDirection(),
		LastCalculatedControlRecoil,
		bHasActiveRecoilProfile ? ActiveRecoilProfile.ControlKickInterpSpeed : 0.0f,
		LastWeaponCameraShakeScale,
		LastWeaponCameraShakeDuration);

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
	{
		Shooter->HandleFireShotAnimation();
	}

	StartAttackCooldown();
	StartReuseCooldown();

	if (BurstShotCount > 0 && CurrentBurstShotCount >= BurstShotCount)
	{
		StopAttack();
		StartPostBurstCooldown();
	}

	if (!bInfiniteAmmo && CurrentAmmo == 0 && CanReload())
	{
		if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(WeaponOwner))
		{
			Shooter->HandleAutoReloadRequested();
		}
		else if (APartnerCharacter* Partner = Cast<APartnerCharacter>(WeaponOwner))
		{
			Partner->HandleAutoReloadRequested();
		}
	}

}
