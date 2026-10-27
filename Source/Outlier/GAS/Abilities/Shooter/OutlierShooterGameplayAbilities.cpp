#include "GAS/Abilities/Shooter/OutlierShooterGameplayAbilities.h"

#include "Audio/OutlierAbilityAudioSettings.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "Weapon/RangedWeaponBase.h"

namespace
{
void PlayShooterAbilityAudioAtLocationFromServer(AActor* EmitterActor, const FGameplayTag& ContextTag)
{
	const UOutlierAbilityAudioSettings* Settings = GetDefault<UOutlierAbilityAudioSettings>();
	if (!EmitterActor || !Settings->PlayerTypeTag.IsValid() || !ContextTag.IsValid())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[GAS.AbilityAudio] Invalid ini configuration. Emitter=%s Type=%s Context=%s"),
			*GetNameSafe(EmitterActor),
			*Settings->PlayerTypeTag.ToString(),
			*ContextTag.ToString());
		return;
	}

	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		EmitterActor,
		Settings->PlayerTypeTag,
		ContextTag);
}

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

bool UOutlierShooterGameplayAbility::PassesUpgradeGrantGate(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTag& AbilityTag) const
{
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	if (!AbilitySystem || !AbilityTag.IsValid())
	{
		return false;
	}

	// 판정 규칙은 ASC 한 곳에 둔다( UI 도 같은 규칙을 쓴다 ). 발동은 ASC 에 실제로 붙은 태그를 근거로 삼는다.
	return AbilitySystem->IsShooterSuitAbilityGranted(
		AbilityTag,
		AbilitySystem->HasMatchingGameplayTag(AbilityTag));
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
	if (!PassesUpgradeGrantGate(ActorInfo, OutlierGameplayTags::Ability::Shooter::QuantumLeap()))
	{
		return false;
	}
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
	// 재충전 중이어도 남은 횟수로 도약할 수 있다. 실패 후 재시도 제한은 별도로 적용한다.
	if (AbilitySystem->GetShooterQuantumLeapChargeState().CurrentCharges <= 0 || AbilitySystem->IsShooterQuantumLeapRetryActive())
	{
		return false;
	}
	if (!Shooter->IsSuitUsable())
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

	QuantumLeapStateHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterQuantumLeapGameplayEffect::StaticClass(),
		ShooterASC->GetShooterSuitConfig().QuantumLeap.CastTimeSeconds,
		Shooter);
	if (!QuantumLeapStateHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	PlayShooterAbilityAudioAtLocationFromServer(
		Shooter,
		GetDefault<UOutlierAbilityAudioSettings>()->ShooterQuantumLeap);


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
	if (ShooterASC && QuantumLeapStateHandle.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(QuantumLeapStateHandle);
	}
	QuantumLeapStateHandle.Invalidate();

	if (ShooterASC && (bTeleportSucceeded || bCommitFailureCooldown))
	{
		// 성공은 충전 1회를 소모한다. 실패는 충전 주기를 건드리지 않고 재시도만 0.5초 제한한다.
		const bool bDelayCommitted = bTeleportSucceeded
			? ShooterASC->CommitShooterQuantumLeapCooldown()
			: ShooterASC->CommitShooterQuantumLeapRetryDelay();
		ensureMsgf(bDelayCommitted, TEXT("Quantum Leap must commit a success cooldown or failure retry delay after a resolved attempt."));
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
	const bool bSuitUnusable = !Shooter->IsSuitUsable();
	const bool bPartnerRebooting = PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Partner->GetActorLocation());
	const bool bOutOfRange = Distance > ShooterASC->GetShooterSuitConfig().MaxPartnerDistance;
	const bool bDestinationBlocked = !IsDestinationClear(*Shooter, *Partner, Destination);
	if (bSuitUnusable || bPartnerRebooting || bOutOfRange || bDestinationBlocked)
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
		// EndAbility가 시전 무적을 지우기 전에 성공 후 무적으로 이어 준다.
		ShooterASC->RefreshShooterQuantumLeapImmunity();
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
	if (!PassesUpgradeGrantGate(ActorInfo, OutlierGameplayTags::Ability::Shooter::BulletReflection()))
	{
		return false;
	}
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
		&& Shooter->IsSuitUsable()
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
		return;
	}
	const UOutlierAbilityAudioSettings* AudioSettings = GetDefault<UOutlierAbilityAudioSettings>();
	PlayShooterAbilityAudioAtLocationFromServer(Shooter, AudioSettings->ShooterReflectionOn);
	PlayShooterAbilityAudioAtLocationFromServer(Shooter, AudioSettings->ShooterReflectionLoop);
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
	PlayShooterAbilityAudioAtLocationFromServer(
		GetShooterCharacter(),
		GetDefault<UOutlierAbilityAudioSettings>()->ShooterReflectionOff);
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
	if (!PassesUpgradeGrantGate(ActorInfo, OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()))
	{
		return false;
	}
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
		&& Shooter->IsSuitUsable()
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

	const UOutlierAbilityAudioSettings* AudioSettings = GetDefault<UOutlierAbilityAudioSettings>();
	PlayShooterAbilityAudioAtLocationFromServer(Shooter, AudioSettings->ShooterOverchargeOn);
	PlayShooterAbilityAudioAtLocationFromServer(Shooter, AudioSettings->ShooterOverchargeLoop);

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
	PlayShooterAbilityAudioAtLocationFromServer(
		Shooter,
		GetDefault<UOutlierAbilityAudioSettings>()->ShooterOverchargeOff);
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
		ShooterASC->ApplyPartnerShieldDeltaToSelf(-PartnerDrain, 0.0f, EOutlierVitalityUpdatePolicy::Immediate);
		RemainingDrain -= PartnerDrain;
	}
	if (RemainingDrain > 0.0f && Shooter->GetCurShield() > 0.0f)
	{
		ShooterASC->ApplyShieldDeltaToSelf(-FMath::Min(Shooter->GetCurShield(), RemainingDrain), EOutlierVitalityUpdatePolicy::Immediate);
	}
	// 실드 소진은 과부하의 비용만 확정하며, 활성 수명은 8초 DataTable GameplayEffect가 소유한다.
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
	if (!bRequestAutomaticStealth && !PassesUpgradeGrantGate(ActorInfo, OutlierGameplayTags::Ability::Shooter::Stealth()))
	{
		return false;
	}
	if (!bRequestAutomaticStealth && !Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
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
	if (!Shooter || !AbilitySystem || Shooter->IsDead())
	{
		return false;
	}
	if (!AbilitySystem->IsShooterSuitConfigured())
	{
		return false;
	}
	if (!bRequestAutomaticStealth && AbilitySystem->IsShooterStealthCooldownActive())
	{
		return false;
	}
	if (!Shooter->IsSuitUsable())
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

	bAutomaticStealth = bRequestAutomaticStealth;
	bPassiveStealth = bAutomaticStealth
		&& ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
	if (bAutomaticStealth)
	{
		// 처치형 은신은 실행 중인 다른 슈트와 병행하며 이후 입력도 막지 않는다.
		SetShouldBlockOtherAbilities(false);
	}
	const float Duration = bAutomaticStealth ? 1.5f : ShooterASC->GetShooterSuitConfig().Stealth.DurationSeconds;
	if (!ApplyStealthEffects(Duration))
	{
		EndStealth(false);
		return;
	}
	ShooterASC->BeginShooterStealthSession();

	PlayShooterAbilityAudioAtLocationFromServer(
		Shooter,
		GetDefault<UOutlierAbilityAudioSettings>()->ShooterStealthOn);
	RefreshEnemyDetection();
}

bool UOutlierShooterStealthAbility::ApplyStealthEffects(float Duration)
{
	UOutlierAbilitySystemComponent* ShooterASC = ShooterAbilitySystem.Get();
	UOutlierAbilitySystemComponent* PartnerASC = PartnerAbilitySystem.Get();
	if (!ShooterASC || !PartnerASC || Duration <= 0.0f)
	{
		return false;
	}
	// 새 효과를 먼저 확보해 Pair 전환/시간 갱신 중 은신 태그가 잠시 사라지지 않게 한다.
	const auto ApplyPairEffect = [this, Duration](UOutlierAbilitySystemComponent* TargetASC)
	{
		FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
		Context.AddSourceObject(GetShooterCharacter());
		FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(
			UOutlierShooterStealthGameplayEffect::StaticClass(), 1.0f, Context);
		if (!Spec.IsValid())
		{
			return FActiveGameplayEffectHandle();
		}
		Spec.Data->SetDuration(Duration, true);
		if (bAutomaticStealth)
		{
			Spec.Data->DynamicGrantedTags.AddTag(OutlierGameplayTags::State::ShooterAutomaticStealth());
		}
		if (bPassiveStealth)
		{
			Spec.Data->DynamicGrantedTags.AddTag(OutlierGameplayTags::State::ShooterPassiveStealth());
		}
		return TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	};
	const FActiveGameplayEffectHandle NewShooter = ApplyPairEffect(ShooterASC);
	const FActiveGameplayEffectHandle NewPartner = ApplyPairEffect(PartnerASC);
	if (!NewShooter.IsValid() || !NewPartner.IsValid())
	{
		ShooterASC->RemoveActiveEffectFromSelf(NewShooter);
		PartnerASC->RemoveActiveEffectFromSelf(NewPartner);
		return false;
	}
	const FActiveGameplayEffectHandle PreviousShooter = ShooterStealthHandle;
	const FActiveGameplayEffectHandle PreviousPartner = PartnerStealthHandle;
	const FActiveGameplayEffectHandle PreviousSpeed = StealthSpeedHandle;
	ShooterStealthHandle = NewShooter;
	PartnerStealthHandle = NewPartner;
	StealthSpeedHandle.Invalidate();
	if (ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowStep()))
	{
		StealthSpeedHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
			UOutlierShooterStealthExitSpeedGameplayEffect::StaticClass(), Duration, GetShooterCharacter());
	}
	// 현재 핸들을 교체한 뒤 이전 효과를 제거해야 제거 콜백이 새 은신을 끝내지 않는다.
	ShooterASC->RemoveActiveEffectFromSelf(PreviousShooter);
	PartnerASC->RemoveActiveEffectFromSelf(PreviousPartner);
	ShooterASC->RemoveActiveEffectFromSelf(PreviousSpeed);
	return true;
}

bool UOutlierShooterStealthAbility::TriggerAutomaticStealth()
{
	UOutlierAbilitySystemComponent* AbilitySystem = GetOutlierAbilitySystem();
	if (!AbilitySystem || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return false;
	}
	bRequestAutomaticStealth = true;
	if (!CanActivateAbility(CurrentSpecHandle, CurrentActorInfo))
	{
		bRequestAutomaticStealth = false;
		return false;
	}
	if (!IsActive())
	{
		const bool bActivated = AbilitySystem->TryActivateAbility(CurrentSpecHandle);
		bRequestAutomaticStealth = false;
		return bActivated;
	}
	bRequestAutomaticStealth = false;
	const bool bWasAutomatic = bAutomaticStealth;
	const bool bWasPassive = bPassiveStealth;
	bAutomaticStealth = true;
	bPassiveStealth = AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowChain());
	if (!ApplyStealthEffects(1.5f))
	{
		bAutomaticStealth = bWasAutomatic;
		bPassiveStealth = bWasPassive;
		return false;
	}
	if (!bWasAutomatic)
	{
		// 일반 은신 종료의 쿨타임만 시작한다. 연속 은신의 처치 자격과 이동 버프는 유지한다.
		AbilitySystem->CommitShooterStealthCooldown();
		SetShouldBlockOtherAbilities(false);
	}
	return true;
}

void UOutlierShooterStealthAbility::NotifyOffensiveAction()
{
	if (IsActive() && !bPassiveStealth)
	{
		EndStealth(true);
	}
}

void UOutlierShooterStealthAbility::NotifyIncomingDamage()
{
	if (IsActive() && !bPassiveStealth)
	{
		EndStealth(true);
	}
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
	const bool bShouldPlayStealthOff = bCommitCooldownOnEnd
		|| (ShooterStealthHandle.IsValid() && PartnerStealthHandle.IsValid());
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
	if (bCommitCooldownOnEnd && ShooterASC && !bAutomaticStealth)
	{
		bCooldownCommitted = ShooterASC->CommitShooterStealthCooldown();
		ensureMsgf(bCooldownCommitted, TEXT("Shooter stealth must commit its configured cooldown on a gameplay end."));
	}
	if (ShooterASC)
	{
		if (bCommitCooldownOnEnd)
		{
			ShooterASC->ApplyShooterStealthExitSpeedBoost();
		}
		ShooterASC->RemoveActiveEffectFromSelf(StealthSpeedHandle);
		ShooterASC->FinishShooterStealthSession();
	}
	StealthSpeedHandle.Invalidate();
	ShooterAbilitySystem.Reset();
	PartnerAbilitySystem.Reset();
	if (bShouldPlayStealthOff)
	{
		PlayShooterAbilityAudioAtLocationFromServer(
			GetShooterCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->ShooterStealthOff);
	}
	bCommitCooldownOnEnd = false;
	bAutomaticStealth = false;
	bPassiveStealth = false;
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
