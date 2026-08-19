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
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=GASBlockOrDead Shooter=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr));
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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=MissingShooterOrASC"));
		return false;
	}
	if (!AbilitySystem->IsShooterSuitConfigured())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=NotConfigured Shooter=%s"), *GetNameSafe(Shooter));
		return false;
	}
	if (AbilitySystem->IsShooterQuantumLeapCooldownActive())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=Cooldown Shooter=%s Remaining=%.2f"), *GetNameSafe(Shooter), AbilitySystem->GetShooterQuantumLeapCooldownRemaining());
		return false;
	}
	if (Shooter->IsSuitDisabledByPartnerBoundary())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=PartnerBoundary Shooter=%s"), *GetNameSafe(Shooter));
		return false;
	}
	if (!IsValid(Partner) || !PartnerASC)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=MissingPartnerOrASC Shooter=%s Partner=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
		return false;
	}
	if (PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=PartnerRebooting Shooter=%s Partner=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
		return false;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Partner->GetActorLocation());
	const float MaxDistance = AbilitySystem->GetShooterSuitConfig().MaxPartnerDistance;
	if (Distance > MaxDistance)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=0 Reason=OutOfRange Shooter=%s Partner=%s Distance=%.1f Max=%.1f"), *GetNameSafe(Shooter), *GetNameSafe(Partner), Distance, MaxDistance);
		return false;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CanActivate=1 Shooter=%s Partner=%s Distance=%.1f"), *GetNameSafe(Shooter), *GetNameSafe(Partner), Distance);
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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] ActivateFailed Reason=RuntimeContextInvalid Shooter=%s Partner=%s ASC=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner), *GetNameSafe(ShooterASC));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!TryResolveDestination(*Shooter, *Partner, Destination))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] ActivateFailed Reason=NoClearDestination Shooter=%s Partner=%s FailureCooldown=1"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
		bCommitFailureCooldown = true;
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	DamageImmuneHandle = ShooterASC->ApplyDamageImmuneStateToSelf();
	if (!DamageImmuneHandle.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] ActivateFailed Reason=DamageImmuneApplyFailed Shooter=%s"), *GetNameSafe(Shooter));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] Activated Shooter=%s Partner=%s Destination=%s CastTime=%.2f Stealthed=%d"),
		*GetNameSafe(Shooter),
		*GetNameSafe(Partner),
		*Destination.ToCompactString(),
		ShooterASC->GetShooterSuitConfig().QuantumLeap.CastTimeSeconds,
		ShooterASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()) ? 1 : 0);

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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CancelIgnored Reason=NotActive FailureCooldown=%d"), bInCommitFailureCooldown ? 1 : 0);
		return false;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CancelRequested Shooter=%s FailureCooldown=%d"), *GetNameSafe(GetShooterCharacter()), bInCommitFailureCooldown ? 1 : 0);
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
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] Ended Shooter=%s Cancelled=%d TeleportSucceeded=%d FailureCooldown=%d CooldownMultiplier=%.2f CooldownCommitted=%d"),
		*GetNameSafe(Shooter),
		bWasCancelled ? 1 : 0,
		bTeleportSucceeded ? 1 : 0,
		bCommitFailureCooldown ? 1 : 0,
		CooldownMultiplier,
		bCooldownCommitted ? 1 : 0);

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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CompleteFailed Reason=InvalidRuntimeOrDead Shooter=%s Dead=%d Partner=%s FailureCooldown=0"), *GetNameSafe(Shooter), Shooter && Shooter->IsDead() ? 1 : 0, *GetNameSafe(Partner));
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const UAbilitySystemComponent* PartnerASC = Partner->GetAbilitySystemComponent();
	if (!PartnerASC)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CompleteFailed Reason=MissingPartnerASC Shooter=%s Partner=%s FailureCooldown=0"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
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
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] CompleteFailed Reason=Revalidation Boundary=%d PartnerRebooting=%d OutOfRange=%d DestinationBlocked=%d Distance=%.1f FailureCooldown=1"),
			bBoundaryDisabled ? 1 : 0,
			bPartnerRebooting ? 1 : 0,
			bOutOfRange ? 1 : 0,
			bDestinationBlocked ? 1 : 0,
			Distance);
		bCommitFailureCooldown = true;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const FRotator PreservedRotation = Shooter->GetActorRotation();
	bTeleportSucceeded = Shooter->TeleportTo(Destination, PreservedRotation, false, true);
	if (!bTeleportSucceeded)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] TeleportFailed Shooter=%s Destination=%s FailureCooldown=1"), *GetNameSafe(Shooter), *Destination.ToCompactString());
		bCommitFailureCooldown = true;
	}
	else
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][QuantumLeap] TeleportSucceeded Shooter=%s Destination=%s"), *GetNameSafe(Shooter), *Destination.ToCompactString());
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
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=GASBlockOrDead Shooter=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr));
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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=MissingShooterOrASC"));
		return false;
	}
	if (!AbilitySystem->IsShooterSuitConfigured())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=NotConfigured Shooter=%s"), *GetNameSafe(Shooter));
		return false;
	}
	if (AbilitySystem->IsShooterStealthCooldownActive())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=Cooldown Shooter=%s Remaining=%.2f"), *GetNameSafe(Shooter), AbilitySystem->GetShooterStealthCooldownRemaining());
		return false;
	}
	if (Shooter->IsSuitDisabledByPartnerBoundary())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=PartnerBoundary Shooter=%s"), *GetNameSafe(Shooter));
		return false;
	}
	if (!IsValid(Partner) || !PartnerASC)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=MissingPartnerOrASC Shooter=%s Partner=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
		return false;
	}
	if (PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=0 Reason=PartnerRebooting Shooter=%s Partner=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
		return false;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] CanActivate=1 Shooter=%s Partner=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner));
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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] ActivateFailed Reason=RuntimeContextInvalid Shooter=%s Partner=%s ShooterASC=%s PartnerASC=%s"), *GetNameSafe(Shooter), *GetNameSafe(Partner), *GetNameSafe(ShooterASC), *GetNameSafe(PartnerASC));
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
	ShooterStealthHandle = ShooterASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), Duration, Shooter);
	PartnerStealthHandle = PartnerASC->ApplyTimedGameplayEffectToSelf(
		UOutlierShooterStealthGameplayEffect::StaticClass(), Duration, Shooter);
	if (!ShooterStealthHandle.IsValid() || !PartnerStealthHandle.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] ActivateFailed Reason=EffectApplyFailed ShooterEffect=%d PartnerEffect=%d"), ShooterStealthHandle.IsValid() ? 1 : 0, PartnerStealthHandle.IsValid() ? 1 : 0);
		EndStealth(false);
		return;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] Activated Shooter=%s Partner=%s Duration=%.2f ShooterEffect=1 PartnerEffect=1"), *GetNameSafe(Shooter), *GetNameSafe(Partner), Duration);
	RefreshEnemyDetection();
}

bool UOutlierShooterStealthAbility::EndStealth(bool bCommitCooldown)
{
	if (!IsActive() || bEndingStealth)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] EndIgnored Active=%d Ending=%d CommitCooldown=%d"), IsActive() ? 1 : 0, bEndingStealth ? 1 : 0, bCommitCooldown ? 1 : 0);
		return false;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] EndRequested Shooter=%s CommitCooldown=%d"), *GetNameSafe(GetShooterCharacter()), bCommitCooldown ? 1 : 0);
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
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[GAS.ShooterSuit.Trace][Stealth] Ended Shooter=%s Cancelled=%d CommitCooldown=%d CooldownCommitted=%d"),
		*GetNameSafe(GetShooterCharacter()),
		bWasCancelled ? 1 : 0,
		bCommitCooldownOnEnd ? 1 : 0,
		bCooldownCommitted ? 1 : 0);
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
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] EffectRemoved Side=Shooter Shooter=%s"), *GetNameSafe(GetShooterCharacter()));
		ShooterStealthHandle.Invalidate();
		EndStealth(true);
	}
}

void UOutlierShooterStealthAbility::HandlePartnerEffectRemoved(
	const FActiveGameplayEffect& RemovedEffect)
{
	if (RemovedEffect.Handle == PartnerStealthHandle)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GAS.ShooterSuit.Trace][Stealth] EffectRemoved Side=Partner Shooter=%s"), *GetNameSafe(GetShooterCharacter()));
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
