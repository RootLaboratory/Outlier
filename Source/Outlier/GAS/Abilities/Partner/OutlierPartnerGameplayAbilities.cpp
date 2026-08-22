#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
constexpr float HackCancellationCooldownScale = 0.5f;

FGameplayTagContainer MakeAllPartnerAbilityTags()
{
	FGameplayTagContainer Tags;
	Tags.AddTag(OutlierGameplayTags::Ability::Partner::EMP());
	Tags.AddTag(OutlierGameplayTags::Ability::Partner::Shield());
	Tags.AddTag(OutlierGameplayTags::Ability::Partner::Hacking());
	Tags.AddTag(OutlierGameplayTags::Ability::Partner::Scan());
	return Tags;
}
}

UOutlierPartnerGameplayAbility::UOutlierPartnerGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	bRetriggerInstancedAbility = false;
	BlockAbilitiesWithTag = MakeAllPartnerAbilityTags();
	ActivationBlockedTags.AddTag(OutlierGameplayTags::State::Rebooting());
}

bool UOutlierPartnerGameplayAbility::BlocksPartnerAbilityExecution() const
{
	return BlockAbilitiesWithTag.HasAllExact(MakeAllPartnerAbilityTags());
}

void UOutlierPartnerGameplayAbility::ConfigurePartnerAbilityTags(
	const FGameplayTag& AbilityTag,
	const FGameplayTag& InCooldownTag)
{
	FGameplayTagContainer Tags;
	Tags.AddTag(AbilityTag);
	SetAssetTags(Tags);
	CooldownTag = InCooldownTag;
}

bool UOutlierPartnerGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const APartnerCharacter* Partner = ActorInfo
		? Cast<APartnerCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UOutlierAbilitySystemComponent* AbilitySystem = ActorInfo
		? Cast<UOutlierAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	return Partner
		&& AbilitySystem
		&& Partner->CanAcceptInput()
		&& Partner->GetController()
		&& Partner->GetController()->GetPawn() == Partner
		&& !AbilitySystem->ArePartnerSkillCooldownsSuspended()
		&& !AbilitySystem->IsPartnerCooldownActive(CooldownTag);
}

APartnerCharacter* UOutlierPartnerGameplayAbility::GetPartnerCharacter() const
{
	return Cast<APartnerCharacter>(GetAvatarActorFromActorInfo());
}

UOutlierAbilitySystemComponent* UOutlierPartnerGameplayAbility::GetOutlierAbilitySystem() const
{
	return Cast<UOutlierAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

bool UOutlierPartnerGameplayAbility::CommitConfiguredCooldown(float OverrideDuration) const
{
	UOutlierAbilitySystemComponent* AbilitySystem = GetOutlierAbilitySystem();
	const bool bCommitted = AbilitySystem
		&& AbilitySystem->CommitPartnerCooldown(CooldownTag, OverrideDuration);
#if UE_BUILD_SHIPPING
	if (!bCommitted)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[PartnerAbility] Failed to commit cooldown %s for %s"),
			*CooldownTag.ToString(),
			*GetNameSafe(GetClass()));
	}
#else
	checkf(
		bCommitted,
		TEXT("[PartnerAbility] Failed to commit cooldown %s for %s"),
		*CooldownTag.ToString(),
		*GetNameSafe(GetClass()));
#endif
	return bCommitted;
}

UOutlierPartnerEMPAbility::UOutlierPartnerEMPAbility()
{
	ConfigurePartnerAbilityTags(
		OutlierGameplayTags::Ability::Partner::EMP(),
		OutlierGameplayTags::Cooldown::Partner::EMP());
}

void UOutlierPartnerEMPAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	APartnerCharacter* Partner = GetPartnerCharacter();
	UPartnerEMPComponent* Component = Partner
		? Partner->FindComponentByClass<UPartnerEMPComponent>()
		: nullptr;
	if (!Component)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveEMPComponent = Component;
	EMPFinishedHandle = Component->OnEMPFinished.AddUObject(
		this,
		&UOutlierPartnerEMPAbility::HandleEMPFinished);
	Component->TryEMP();
	if (!Component->IsEMPInteractionActive() || !CommitConfiguredCooldown())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UOutlierPartnerEMPAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UPartnerEMPComponent* Component = ActiveEMPComponent.Get())
	{
		Component->OnEMPFinished.Remove(EMPFinishedHandle);
		if (bWasCancelled && Component->IsEMPInteractionActive())
		{
			Component->CancelForReboot();
		}
	}
	ActiveEMPComponent.Reset();
	EMPFinishedHandle.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierPartnerEMPAbility::HandleEMPFinished(bool bAppliedTargets, bool bCancelled)
{
	(void)bAppliedTargets;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
}

UOutlierPartnerShieldAbility::UOutlierPartnerShieldAbility()
{
	ConfigurePartnerAbilityTags(
		OutlierGameplayTags::Ability::Partner::Shield(),
		OutlierGameplayTags::Cooldown::Partner::Shield());
}

void UOutlierPartnerShieldAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	APartnerCharacter* Partner = GetPartnerCharacter();
	UPartnerSupportComponent* Component = Partner
		? Partner->FindComponentByClass<UPartnerSupportComponent>()
		: nullptr;
	const bool bSucceeded = Component && Component->TryShield_Server() == EPartnerSkillUseResult::Success;
	const bool bCommitted = bSucceeded && CommitConfiguredCooldown();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bCommitted);
}

UOutlierPartnerHackAbility::UOutlierPartnerHackAbility()
{
	ConfigurePartnerAbilityTags(
		OutlierGameplayTags::Ability::Partner::Hacking(),
		OutlierGameplayTags::Cooldown::Partner::Hacking());
}

void UOutlierPartnerHackAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	APartnerCharacter* Partner = GetPartnerCharacter();
	UPartnerHackComponent* Component = Partner
		? Partner->FindComponentByClass<UPartnerHackComponent>()
		: nullptr;
	if (!Component)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveHackComponent = Component;
	HackFinishedHandle = Component->OnHackFinished.AddUObject(
		this,
		&UOutlierPartnerHackAbility::HandleHackFinished);
	Component->TryHack();
	if (!Component->IsHackInteractionActive())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UOutlierPartnerHackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UPartnerHackComponent* Component = ActiveHackComponent.Get())
	{
		Component->OnHackFinished.Remove(HackFinishedHandle);
		if (bWasCancelled && Component->IsHackInteractionActive())
		{
			Component->CancelForReboot();
		}
	}
	ActiveHackComponent.Reset();
	HackFinishedHandle.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierPartnerHackAbility::HandleHackFinished(
	EHackResult Result,
	bool bPossessionTarget)
{
	const bool bFullCooldown = Result == EHackResult::Fail
		|| (Result == EHackResult::Success && !bPossessionTarget);
	const bool bCancelled = Result == EHackResult::Cancelled;
	bool bCommitted = true;
	if (bFullCooldown)
	{
		bCommitted = CommitConfiguredCooldown();
	}
	else if (bCancelled)
	{
		const UOutlierAbilitySystemComponent* AbilitySystem = GetOutlierAbilitySystem();
		const float CancellationCooldown = AbilitySystem
			? AbilitySystem->GetPartnerAbilityConfig().HackCooldown * HackCancellationCooldownScale
			: 0.0f;
		bCommitted = CommitConfiguredCooldown(CancellationCooldown);
	}
	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		Result != EHackResult::Success || !bCommitted);
}

UOutlierPartnerScanAbility::UOutlierPartnerScanAbility()
{
	ConfigurePartnerAbilityTags(
		OutlierGameplayTags::Ability::Partner::Scan(),
		OutlierGameplayTags::Cooldown::Partner::Scan());
}

void UOutlierPartnerScanAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	APartnerCharacter* Partner = GetPartnerCharacter();
	UPartnerSupportComponent* Component = Partner
		? Partner->FindComponentByClass<UPartnerSupportComponent>()
		: nullptr;
	if (!Component)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveSupportComponent = Component;
	ScanFinishedHandle = Component->OnScanFinished.AddUObject(
		this,
		&UOutlierPartnerScanAbility::HandleScanFinished);
	const bool bStarted = Component->TryScan_Server() == EPartnerSkillUseResult::Success;
	if (!bStarted || !CommitConfiguredCooldown())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UOutlierPartnerScanAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UPartnerSupportComponent* Component = ActiveSupportComponent.Get())
	{
		Component->OnScanFinished.Remove(ScanFinishedHandle);
		if (bWasCancelled)
		{
			Component->CancelForReboot();
		}
	}
	ActiveSupportComponent.Reset();
	ScanFinishedHandle.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierPartnerScanAbility::HandleScanFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
