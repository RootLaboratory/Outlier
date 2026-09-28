#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"

#include "Audio/OutlierAbilityAudioSettings.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
constexpr float HackCancellationCooldownScale = 0.5f;

bool PlayAbilityAudioAtLocationFromServer(AActor* EmitterActor, const FGameplayTag& ContextTag)
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
		return false;
	}

	const bool bAccepted = UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		EmitterActor,
		Settings->PlayerTypeTag,
		ContextTag);
	/*UE_LOG(
		LogTemp,
		Display,
		TEXT("[PartnerAbilityAudioDebug] Play Emitter=%s Context=%s Accepted=%d"),
		*GetNameSafe(EmitterActor),
		*ContextTag.ToString(),
		bAccepted ? 1 : 0);*/
	return bAccepted;
}

bool StopAbilityAudioLoopAtLocationFromServer(AActor* EmitterActor, const FGameplayTag& ContextTag)
{
	const UOutlierAbilityAudioSettings* Settings = GetDefault<UOutlierAbilityAudioSettings>();
	if (!EmitterActor || !Settings->PlayerTypeTag.IsValid() || !ContextTag.IsValid())
	{
		return false;
	}

	const bool bStopped = UOutlierAudioSubsystem::StopTaggedAtLocationFromServer(
		EmitterActor,
		Settings->PlayerTypeTag,
		ContextTag);
	/*UE_LOG(
		LogTemp,
		Display,
		TEXT("[PartnerAbilityAudioDebug] Stop Emitter=%s Context=%s Stopped=%d"),
		*GetNameSafe(EmitterActor),
		*ContextTag.ToString(),
		bStopped ? 1 : 0);*/
	return bStopped;
}

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
	// Partner 능력도 슈트에 의존한다. 플래그는 Shooter PlayerState 에만 서므로
	// 페어를 거슬러 올라가 확인한다(IsPairSuitAcquired).
	const AOutlierPlayerState* PartnerPS = Partner
		? Partner->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	return Partner
		&& AbilitySystem
		&& PartnerPS
		&& PartnerPS->IsPairSuitAcquired()
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
		return;
	}
	PlayAbilityAudioAtLocationFromServer(
		Partner,
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPCharge);
}

void UOutlierPartnerEMPAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	const bool bStoppedChargeAudio = StopAbilityAudioLoopAtLocationFromServer(
		GetPartnerCharacter(),
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPCharge);
	// 아래 디버그 로그 전용 값이다. 로그를 되살리면 이 줄을 지운다.
	(void)bStoppedChargeAudio;
	/*UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerAbilityAudioDebug] EMP End Cancelled=%d ChargeLoopStopped=%d ActiveComponent=%d"),
		bWasCancelled ? 1 : 0,
		bStoppedChargeAudio ? 1 : 0,
		ActiveEMPComponent.IsValid() ? 1 : 0);*/

	bool bCancelledActiveEMP = false;
	if (UPartnerEMPComponent* Component = ActiveEMPComponent.Get())
	{
		Component->OnEMPFinished.Remove(EMPFinishedHandle);
		bCancelledActiveEMP = bWasCancelled && Component->IsEMPInteractionActive();
		if (bCancelledActiveEMP)
		{
			Component->CancelForReboot();
		}
	}
	ActiveEMPComponent.Reset();
	EMPFinishedHandle.Reset();
	if (bCancelledActiveEMP)
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPFail);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierPartnerEMPAbility::HandleEMPFinished(bool bAppliedTargets, bool bCancelled)
{
	StopAbilityAudioLoopAtLocationFromServer(
		GetPartnerCharacter(),
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPCharge);

	if (bAppliedTargets && !bCancelled)
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPBurst);

		UE_LOG(LogTemp, Error, TEXT("PREFinishcall called"));
	}
	else
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerEMPFail);

		UE_LOG(LogTemp, Error, TEXT("PREFinishFailed called"));

	}
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
	if (bSucceeded && bCommitted)
	{
		PlayAbilityAudioAtLocationFromServer(
			Partner,
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerShield);
	}
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
		return;
	}
	PlayAbilityAudioAtLocationFromServer(
		Partner,
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackTryLoop);
}

void UOutlierPartnerHackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	const bool bStoppedHackAudio = StopAbilityAudioLoopAtLocationFromServer(
		GetPartnerCharacter(),
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackTryLoop);
	// 아래 디버그 로그 전용 값이다. 로그를 되살리면 이 줄을 지운다.
	(void)bStoppedHackAudio;
	/*UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerAbilityAudioDebug] Hack End Cancelled=%d TryLoopStopped=%d ActiveComponent=%d"),
		bWasCancelled ? 1 : 0,
		bStoppedHackAudio ? 1 : 0,
		ActiveHackComponent.IsValid() ? 1 : 0);*/

	bool bCancelledActiveHack = false;
	if (UPartnerHackComponent* Component = ActiveHackComponent.Get())
	{
		Component->OnHackFinished.Remove(HackFinishedHandle);
		bCancelledActiveHack = bWasCancelled && Component->IsHackInteractionActive();
		if (bCancelledActiveHack)
		{
			Component->CancelForReboot();
		}
	}
	ActiveHackComponent.Reset();
	HackFinishedHandle.Reset();
	if (bCancelledActiveHack)
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackFail);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOutlierPartnerHackAbility::HandleHackFinished(
	EHackResult Result,
	bool bPossessionTarget)
{
	StopAbilityAudioLoopAtLocationFromServer(
		GetPartnerCharacter(),
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackTryLoop);

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
	if (Result == EHackResult::Success)
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackSuccess);
	}
	else if (Result == EHackResult::Fail)
	{
		PlayAbilityAudioAtLocationFromServer(
			GetPartnerCharacter(),
			GetDefault<UOutlierAbilityAudioSettings>()->PartnerHackFail);
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
		return;
	}
	PlayAbilityAudioAtLocationFromServer(
		Partner,
		GetDefault<UOutlierAbilityAudioSettings>()->PartnerScan);
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
