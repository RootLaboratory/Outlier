#include "GAS/Abilities/Shooter/OutlierShooterGameplayAbilities.h"

#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
FGameplayTagContainer MakeAllShooterSuitAbilityTags()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::QuantumLeap());
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::BulletReflection());
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::Stealth());
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::WeaponOvercharge());
	return Tags;
}
}

UOutlierShooterGameplayAbility::UOutlierShooterGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	bRetriggerInstancedAbility = false;
	BlockAbilitiesWithTag = MakeAllShooterSuitAbilityTags();
	ActivationBlockedTags.AddTag(OutlierGameplayTags::State::Dead());
}

AShooterCharacter* UOutlierShooterGameplayAbility::GetShooterCharacter() const
{
	return Cast<AShooterCharacter>(GetAvatarActorFromActorInfo());
}

UOutlierAbilitySystemComponent* UOutlierShooterGameplayAbility::GetOutlierAbilitySystem() const
{
	return Cast<UOutlierAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

UOutlierShooterQuantumLeapAbility::UOutlierShooterQuantumLeapAbility()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::QuantumLeap());
	SetAssetTags(Tags);
}

bool UOutlierShooterQuantumLeapAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AShooterCharacter* Shooter = ActorInfo
		? Cast<AShooterCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	const UAbilitySystemComponent* PartnerASC = Partner ? Partner->GetAbilitySystemComponent() : nullptr;
	if (!Shooter || !AbilitySystem)
	{
		return false;
	}
	if (!AbilitySystem->IsShooterSuitConfigured())
	{
		return false;
	}
	if (AbilitySystem->IsShooterQuantumLeapCooldownActive())
	{
		return false;
	}
	if (Shooter->IsSuitDisabledByPartnerBoundary())
	{
		return false;
	}
	if (!IsValid(Partner) || !PartnerASC)
	{
		return false;
	}
	if (PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))
	{
		return false;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Partner->GetActorLocation());
	const float MaxDistance = AbilitySystem->GetShooterSuitConfig().MaxPartnerDistance;
	if (Distance > MaxDistance)
	{
		return false;
	}
	return true;
}

void UOutlierShooterQuantumLeapAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bCommitFailureCooldown = false;
	bTeleportSucceeded = false;
	AShooterCharacter* Shooter = GetShooterCharacter();
	APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	if (!Shooter || !Partner || !ShooterASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!TryResolveDestination(*Shooter, *Partner, Destination))
	{
		bCommitFailureCooldown = true;
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	DamageImmuneHandle = ShooterASC->ApplyDamageImmuneStateToSelf();
	if (!DamageImmuneHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}


	Shooter->GetWorldTimerManager().SetTimer(
		CastTimerHandle,
		this,
		&UOutlierShooterQuantumLeapAbility::CompleteQuantumLeap,
		ShooterASC->GetShooterSuitConfig().QuantumLeap.CastTimeSeconds,
		false);
}

bool UOutlierShooterQuantumLeapAbility::CancelQuantumLeap(bool bInCommitFailureCooldown)
{
	if (!IsActive())
	{
		return false;
	}
	bCommitFailureCooldown = bInCommitFailureCooldown;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	return true;
}

void UOutlierShooterQuantumLeapAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	AShooterCharacter* Shooter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	if (Shooter)
	{
		Shooter->GetWorldTimerManager().ClearTimer(CastTimerHandle);
	}
	CastTimerHandle.Invalidate();
	if (ShooterASC && DamageImmuneHandle.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(DamageImmuneHandle);
	}
	DamageImmuneHandle.Invalidate();

	bool bCooldownCommitted = false;
	float CooldownMultiplier = 0.0f;
	if (ShooterASC && (bTeleportSucceeded || bCommitFailureCooldown))
	{
		CooldownMultiplier = bTeleportSucceeded ? 1.0f : 0.5f;
		bCooldownCommitted = ShooterASC->CommitShooterQuantumLeapCooldown(CooldownMultiplier);
		ensureMsgf(bCooldownCommitted, TEXT("Quantum Leap must commit its configured cooldown after a resolved attempt."));
	}

	Destination = FVector::ZeroVector;
	bCommitFailureCooldown = false;
	bTeleportSucceeded = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UOutlierShooterQuantumLeapAbility::TryResolveDestination(
	const AShooterCharacter& Shooter,
	const APartnerCharacter& Partner,
	FVector& OutDestination) const
{
	const UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	const UCapsuleComponent* Capsule = Shooter.GetCapsuleComponent();
	UWorld* World = Shooter.GetWorld();
	if (!ShooterASC || !Capsule || !World)
	{
		return false;
	}

	const float Offset = ShooterASC->GetShooterSuitConfig().QuantumLeap.PartnerOffset;
	const FRotator PartnerYaw(0.0f, Partner.GetActorRotation().Yaw, 0.0f);
	const FVector Forward = PartnerYaw.Vector();
	const FVector Right = FRotationMatrix(PartnerYaw).GetUnitAxis(EAxis::Y);
	const FVector Directions[] = {-Forward, -Right, Right, Forward};
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(QuantumLeapGround), false, &Shooter);
	GroundParams.AddIgnoredActor(&Partner);
	for (const FVector& Direction : Directions)
	{
		FVector Candidate = Partner.GetActorLocation() + Direction * Offset;
		FHitResult GroundHit;
		if (World->LineTraceSingleByChannel(
			GroundHit,
			Candidate,
			Candidate - FVector(0.0f, 0.0f, 100.0f),
			ECC_Visibility,
			GroundParams))
		{
			Candidate.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight + 1.0f;
		}

		if (IsDestinationClear(Shooter, Partner, Candidate))
		{
			OutDestination = Candidate;
			return true;
		}
	}
	return false;
}

bool UOutlierShooterQuantumLeapAbility::IsDestinationClear(
	const AShooterCharacter& Shooter,
	const APartnerCharacter& Partner,
	const FVector& InDestination) const
{
	const UCapsuleComponent* Capsule = Shooter.GetCapsuleComponent();
	UWorld* World = Shooter.GetWorld();
	if (!Capsule || !World)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(QuantumLeapDestination), false, &Shooter);
	Params.AddIgnoredActor(&Partner);
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(),
		Capsule->GetScaledCapsuleHalfHeight());
	return !World->OverlapBlockingTestByProfile(
		InDestination,
		Shooter.GetActorQuat(),
		Capsule->GetCollisionProfileName(),
		CapsuleShape,
		Params);
}

void UOutlierShooterQuantumLeapAbility::CompleteQuantumLeap()
{
	AShooterCharacter* Shooter = GetShooterCharacter();
	APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	if (!Shooter || !ShooterASC || Shooter->IsDead() || !IsValid(Partner))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const UAbilitySystemComponent* PartnerASC = Partner->GetAbilitySystemComponent();
	if (!PartnerASC)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	const bool bBoundaryDisabled = Shooter->IsSuitDisabledByPartnerBoundary();
	const bool bPartnerRebooting = PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Partner->GetActorLocation());
	const bool bOutOfRange = Distance > ShooterASC->GetShooterSuitConfig().MaxPartnerDistance;
	const bool bDestinationBlocked = !IsDestinationClear(*Shooter, *Partner, Destination);
	if (bBoundaryDisabled || bPartnerRebooting || bOutOfRange || bDestinationBlocked)
	{
		bCommitFailureCooldown = true;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const FRotator PreservedRotation = Shooter->GetActorRotation();
	bTeleportSucceeded = Shooter->TeleportTo(Destination, PreservedRotation, false, true);
	if (!bTeleportSucceeded)
	{
		bCommitFailureCooldown = true;
	}
	else
	{
		Shooter->ForceNetUpdate();
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bTeleportSucceeded);
}

UOutlierShooterBulletReflectionAbility::UOutlierShooterBulletReflectionAbility()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::BulletReflection());
	SetAssetTags(Tags);
}

bool UOutlierShooterBulletReflectionAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AShooterCharacter* Shooter = ActorInfo
		? Cast<AShooterCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	const UAbilitySystemComponent* PartnerASC = Partner ? Partner->GetAbilitySystemComponent() : nullptr;
	return Shooter
		&& AbilitySystem
		&& AbilitySystem->IsShooterSuitConfigured()
		&& !AbilitySystem->IsShooterBulletReflectionCooldownActive()
		&& !Shooter->IsSuitDisabledByPartnerBoundary()
		&& IsValid(Partner)
		&& PartnerASC
		&& !PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
}

void UOutlierShooterBulletReflectionAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AShooterCharacter* Shooter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	if (!Shooter || !ShooterASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ShooterAbilitySystem = ShooterASC;
	EffectRemovedDelegateHandle = ShooterASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(
		this, &UOutlierShooterBulletReflectionAbility::HandleReflectionEffectRemoved);
	checkf(
		EffectRemovedDelegateHandle.IsValid(),
		TEXT("Shooter bullet reflection removal observer must bind."));

	ReflectionEffectHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterBulletReflectionGameplayEffect::StaticClass(),
		ShooterASC->GetShooterSuitConfig().BulletReflection.DurationSeconds,
		Shooter);
	if (!ReflectionEffectHandle.IsValid())
	{
		EndBulletReflection(false);
	}
}

bool UOutlierShooterBulletReflectionAbility::EndBulletReflection(bool bCommitCooldown)
{
	if (!IsActive() || bEndingReflection)
	{
		return false;
	}
	bCommitCooldownOnEnd = bCommitCooldown;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bCommitCooldown);
	return true;
}

void UOutlierShooterBulletReflectionAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	bEndingReflection = true;
	UOutlierAbilitySystemComponent* ShooterASC = ShooterAbilitySystem.Get();
	if (ShooterASC)
	{
		ShooterASC->OnAnyGameplayEffectRemovedDelegate().Remove(EffectRemovedDelegateHandle);
	}
	EffectRemovedDelegateHandle.Reset();
	if (ShooterASC && ReflectionEffectHandle.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(ReflectionEffectHandle);
	}
	ReflectionEffectHandle.Invalidate();

	if (bCommitCooldownOnEnd && ShooterASC)
	{
		const bool bCooldownCommitted = ShooterASC->CommitShooterBulletReflectionCooldown();
		ensureMsgf(
			bCooldownCommitted,
			TEXT("Shooter bullet reflection must commit its configured cooldown on a gameplay end."));
	}

	ShooterAbilitySystem.Reset();
	bCommitCooldownOnEnd = false;
	bEndingReflection = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierShooterBulletReflectionAbility::HandleReflectionEffectRemoved(
	const FActiveGameplayEffect& RemovedEffect)
{
	if (RemovedEffect.Handle == ReflectionEffectHandle)
	{
		ReflectionEffectHandle.Invalidate();
		EndBulletReflection(true);
	}
}

UOutlierShooterWeaponOverchargeAbility::UOutlierShooterWeaponOverchargeAbility()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::WeaponOvercharge());
	SetAssetTags(Tags);
}

bool UOutlierShooterWeaponOverchargeAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AShooterCharacter* Shooter = ActorInfo
		? Cast<AShooterCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	const UAbilitySystemComponent* PartnerASC = Partner ? Partner->GetAbilitySystemComponent() : nullptr;
	return Shooter
		&& AbilitySystem
		&& AbilitySystem->IsShooterSuitConfigured()
		&& !AbilitySystem->IsShooterWeaponOverchargeCooldownActive()
		&& Shooter->GetWeaponMode() == EWeaponMode::Primary
		&& Cast<ARangedWeaponBase>(Shooter->GetCurrentWeapon())
		&& !Shooter->IsSuitDisabledByPartnerBoundary()
		&& IsValid(Partner)
		&& PartnerASC
		&& !PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
}

void UOutlierShooterWeaponOverchargeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AShooterCharacter* Shooter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	if (!Shooter || !ShooterASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ShooterAbilitySystem = ShooterASC;
	EffectRemovedDelegateHandle = ShooterASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(
		this, &UOutlierShooterWeaponOverchargeAbility::HandleOverchargeEffectRemoved);
	checkf(EffectRemovedDelegateHandle.IsValid(), TEXT("Shooter weapon overcharge removal observer must bind."));

	OverchargeEffectHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterWeaponOverchargeGameplayEffect::StaticClass(),
		ShooterASC->GetShooterSuitConfig().WeaponOvercharge.DurationSeconds,
		Shooter);
	if (!OverchargeEffectHandle.IsValid())
	{
		EndWeaponOvercharge(false);
		return;
	}
	if (!Shooter->BeginWeaponOvercharge())
	{
		EndWeaponOvercharge(false);
		return;
	}

	constexpr float DrainInterval = 0.05f;
	Shooter->GetWorldTimerManager().SetTimer(
		ShieldDrainTimerHandle,
		this,
		&UOutlierShooterWeaponOverchargeAbility::DrainShield,
		DrainInterval,
		true);
}

bool UOutlierShooterWeaponOverchargeAbility::EndWeaponOvercharge(bool bCommitCooldown)
{
	if (!IsActive() || bEndingOvercharge)
	{
		return false;
	}
	bCommitCooldownOnEnd = bCommitCooldown;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bCommitCooldown);
	return true;
}

void UOutlierShooterWeaponOverchargeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	bEndingOvercharge = true;
	AShooterCharacter* Shooter = GetShooterCharacter();
	if (Shooter)
	{
		Shooter->GetWorldTimerManager().ClearTimer(ShieldDrainTimerHandle);
	}

	UOutlierAbilitySystemComponent* ShooterASC = ShooterAbilitySystem.Get();
	if (ShooterASC)
	{
		ShooterASC->OnAnyGameplayEffectRemovedDelegate().Remove(EffectRemovedDelegateHandle);
	}
	EffectRemovedDelegateHandle.Reset();
	if (ShooterASC && OverchargeEffectHandle.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(OverchargeEffectHandle);
	}
	OverchargeEffectHandle.Invalidate();

	if (bCommitCooldownOnEnd && ShooterASC)
	{
		ensureMsgf(
			ShooterASC->CommitShooterWeaponOverchargeCooldown(),
			TEXT("Shooter weapon overcharge must commit its configured cooldown on a gameplay end."));
		if (Shooter)
		{
			Shooter->FinishWeaponOvercharge(
				ShooterASC->GetShooterSuitConfig().WeaponOvercharge.ShieldRecoveryDelay);
		}
	}

	ShooterAbilitySystem.Reset();
	bCommitCooldownOnEnd = false;
	bEndingOvercharge = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierShooterWeaponOverchargeAbility::DrainShield()
{
	AShooterCharacter* Shooter = GetShooterCharacter();
	UOutlierAbilitySystemComponent* ShooterASC = ShooterAbilitySystem.Get();
	if (!Shooter || !ShooterASC)
	{
		EndWeaponOvercharge(false);
		return;
	}

	constexpr float DrainInterval = 0.05f;
	float RemainingDrain = ShooterASC->GetShooterSuitConfig().WeaponOvercharge.ShieldDrainPerSecond * DrainInterval;
	const float PartnerDrain = FMath::Min(Shooter->GetCurPartnerShield(), RemainingDrain);
	if (PartnerDrain > 0.0f)
	{
		ShooterASC->ApplyPartnerShieldDeltaToSelf(-PartnerDrain, 0.0f);
		RemainingDrain -= PartnerDrain;
	}
	if (RemainingDrain > 0.0f && Shooter->GetCurShield() > 0.0f)
	{
		ShooterASC->ApplyShieldDeltaToSelf(-FMath::Min(Shooter->GetCurShield(), RemainingDrain));
	}

	if (Shooter->GetCurPartnerShield() <= KINDA_SMALL_NUMBER
		&& Shooter->GetCurShield() <= KINDA_SMALL_NUMBER)
	{
		// 쉴드가 연료이므로 둘 다 소진된 프레임에 지속시간을 기다리지 않고 종료한다.
		EndWeaponOvercharge(true);
	}
}

void UOutlierShooterWeaponOverchargeAbility::HandleOverchargeEffectRemoved(
	const FActiveGameplayEffect& RemovedEffect)
{
	if (RemovedEffect.Handle == OverchargeEffectHandle)
	{
		OverchargeEffectHandle.Invalidate();
		EndWeaponOvercharge(true);
	}
}

UOutlierShooterStealthAbility::UOutlierShooterStealthAbility()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Shooter::Stealth());
	SetAssetTags(Tags);
}

bool UOutlierShooterStealthAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AShooterCharacter* Shooter = ActorInfo
		? Cast<AShooterCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	const UAbilitySystemComponent* PartnerASC = Partner ? Partner->GetAbilitySystemComponent() : nullptr;
	if (!Shooter || !AbilitySystem)
	{
		return false;
	}
	if (!AbilitySystem->IsShooterSuitConfigured())
	{
		return false;
	}
	if (AbilitySystem->IsShooterStealthCooldownActive())
	{
		return false;
	}
	if (Shooter->IsSuitDisabledByPartnerBoundary())
	{
		return false;
	}
	if (!IsValid(Partner) || !PartnerASC)
	{
		return false;
	}
	if (PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))
	{
		return false;
	}
	return true;
}

void UOutlierShooterStealthAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AShooterCharacter* Shooter = GetShooterCharacter();
	APartnerCharacter* Partner = Shooter ? Shooter->GetPartnerCharacter() : nullptr;
	UOutlierAbilitySystemComponent* ShooterASC = GetOutlierAbilitySystem();
	UOutlierAbilitySystemComponent* PartnerASC = Partner
		? Cast<UOutlierAbilitySystemComponent>(Partner->GetAbilitySystemComponent())
		: nullptr;
	if (!Shooter || !Partner || !ShooterASC || !PartnerASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ShooterAbilitySystem = ShooterASC;
	PartnerAbilitySystem = PartnerASC;
	ShooterEffectRemovedDelegateHandle = ShooterASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(
		this, &UOutlierShooterStealthAbility::HandleShooterEffectRemoved);
	PartnerEffectRemovedDelegateHandle = PartnerASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(
		this, &UOutlierShooterStealthAbility::HandlePartnerEffectRemoved);
	checkf(
		ShooterEffectRemovedDelegateHandle.IsValid() && PartnerEffectRemovedDelegateHandle.IsValid(),
		TEXT("Shooter stealth removal observers must bind to both AbilitySystemComponents."));

	const float Duration = ShooterASC->GetShooterSuitConfig().Stealth.DurationSeconds;
	// 은신은 원래 Pair의 두 ASC에만 적용한다. Partner가 빙의한 Enemy ASC에는 공유하지 않는다.
	ShooterStealthHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), Duration, Shooter);
	PartnerStealthHandle = PartnerASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), Duration, Shooter);
	if (!ShooterStealthHandle.IsValid() || !PartnerStealthHandle.IsValid())
	{
		EndStealth(false);
		return;
	}
	RefreshEnemyDetection();
}

bool UOutlierShooterStealthAbility::EndStealth(bool bCommitCooldown)
{
	if (!IsActive() || bEndingStealth)
	{
		return false;
	}
	bCommitCooldownOnEnd = bCommitCooldown;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bCommitCooldown);
	return true;
}

void UOutlierShooterStealthAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	bEndingStealth = true;
	UOutlierAbilitySystemComponent* ShooterASC = ShooterAbilitySystem.Get();
	UOutlierAbilitySystemComponent* PartnerASC = PartnerAbilitySystem.Get();
	if (ShooterASC)
	{
		ShooterASC->OnAnyGameplayEffectRemovedDelegate().Remove(ShooterEffectRemovedDelegateHandle);
	}
	if (PartnerASC)
	{
		PartnerASC->OnAnyGameplayEffectRemovedDelegate().Remove(PartnerEffectRemovedDelegateHandle);
	}
	ShooterEffectRemovedDelegateHandle.Reset();
	PartnerEffectRemovedDelegateHandle.Reset();

	if (ShooterASC && ShooterStealthHandle.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(ShooterStealthHandle);
	}
	if (PartnerASC && PartnerStealthHandle.IsValid())
	{
		PartnerASC->RemoveActiveEffectFromSelf(PartnerStealthHandle);
	}
	ShooterStealthHandle.Invalidate();
	PartnerStealthHandle.Invalidate();
	RefreshEnemyDetection();

	bool bCooldownCommitted = false;
	if (bCommitCooldownOnEnd && ShooterASC)
	{
		bCooldownCommitted = ShooterASC->CommitShooterStealthCooldown();
		ensureMsgf(bCooldownCommitted, TEXT("Shooter stealth must commit its configured cooldown on a gameplay end."));
	}
	ShooterAbilitySystem.Reset();
	PartnerAbilitySystem.Reset();
	bCommitCooldownOnEnd = false;
	bEndingStealth = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierShooterStealthAbility::HandleShooterEffectRemoved(
	const FActiveGameplayEffect& RemovedEffect)
{
	if (RemovedEffect.Handle == ShooterStealthHandle)
	{
		ShooterStealthHandle.Invalidate();
		EndStealth(true);
	}
}

void UOutlierShooterStealthAbility::HandlePartnerEffectRemoved(
	const FActiveGameplayEffect& RemovedEffect)
{
	if (RemovedEffect.Handle == PartnerStealthHandle)
	{
		PartnerStealthHandle.Invalidate();
		EndStealth(true);
	}
}

void UOutlierShooterStealthAbility::RefreshEnemyDetection() const
{
	AShooterCharacter* Shooter = GetShooterCharacter();
	if (!Shooter || !Shooter->GetWorld())
	{
		return;
	}
	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = Shooter->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		EnemyRoomSubsystem->RefreshDetectionTarget(Shooter);
		if (APartnerCharacter* Partner = Shooter->GetPartnerCharacter())
		{
			EnemyRoomSubsystem->RefreshDetectionTarget(Partner);
		}
	}
}
