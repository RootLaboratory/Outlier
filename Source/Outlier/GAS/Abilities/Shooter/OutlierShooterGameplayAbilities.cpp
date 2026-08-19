#include "GAS/Abilities/Shooter/OutlierShooterGameplayAbilities.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"

UOutlierShooterGameplayAbility::UOutlierShooterGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	bRetriggerInstancedAbility = false;
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
	return Shooter
		&& AbilitySystem
		&& AbilitySystem->IsShooterSuitConfigured()
		&& !AbilitySystem->IsShooterStealthCooldownActive()
		&& !Shooter->IsSuitDisabledByPartnerBoundary()
		&& IsValid(Partner)
		&& PartnerASC
		&& !PartnerASC->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
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

	if (bCommitCooldownOnEnd && ShooterASC)
	{
		const bool bCommitted = ShooterASC->CommitShooterStealthCooldown();
		ensureMsgf(bCommitted, TEXT("Shooter stealth must commit its configured cooldown on a gameplay end."));
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
