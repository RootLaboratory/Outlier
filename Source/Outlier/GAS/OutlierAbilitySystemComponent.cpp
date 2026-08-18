#include "GAS/OutlierAbilitySystemComponent.h"

#include "GameFramework/Pawn.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Outlier.h"

namespace
{
FGameplayEffectSpecHandle MakeSelfEffectSpec(
	UOutlierAbilitySystemComponent& AbilitySystem,
	TSubclassOf<UGameplayEffect> EffectClass,
	AController* Instigator,
	AActor* EffectCauser)
{
	FGameplayEffectContextHandle Context = AbilitySystem.MakeEffectContext();
	Context.AddInstigator(Instigator ? Instigator->GetPawn() : nullptr, EffectCauser);
	Context.AddSourceObject(EffectCauser);
	return AbilitySystem.MakeOutgoingSpec(EffectClass, 1.0f, Context);
}

bool ApplySpecToSelf(UOutlierAbilitySystemComponent& AbilitySystem, FGameplayEffectSpecHandle& SpecHandle)
{
	return SpecHandle.IsValid()
		&& AbilitySystem.ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get()).WasSuccessfullyApplied();
}

FGameplayEffectQuery MakeOwningTagQuery(const FGameplayTag& Tag)
{
	FGameplayTagContainer Tags;
	Tags.AddTag(Tag);
	return FGameplayEffectQuery::MakeQuery_MatchAllOwningTags(Tags);
}
}

bool FOutlierPartnerAbilityConfig::IsValid(FString& OutError) const
{
	if (EMPCooldown <= 0.0f)
	{
		OutError = TEXT("EMP cooldown must be positive");
		return false;
	}
	if (ShieldCooldown <= 0.0f)
	{
		OutError = TEXT("Shield cooldown must be positive");
		return false;
	}
	if (HackCooldown <= 0.0f)
	{
		OutError = TEXT("Hack cooldown must be positive");
		return false;
	}
	if (ScanCooldown <= 0.0f)
	{
		OutError = TEXT("Scan cooldown must be positive");
		return false;
	}
	if (ScanDuration <= 0.0f)
	{
		OutError = TEXT("Scan duration must be positive");
		return false;
	}

	OutError.Reset();
	return true;
}

bool FOutlierPartnerAbilityConfig::Equals(const FOutlierPartnerAbilityConfig& Other) const
{
	return FMath::IsNearlyEqual(EMPCooldown, Other.EMPCooldown)
		&& FMath::IsNearlyEqual(ShieldCooldown, Other.ShieldCooldown)
		&& FMath::IsNearlyEqual(HackCooldown, Other.HackCooldown)
		&& FMath::IsNearlyEqual(ScanCooldown, Other.ScanCooldown)
		&& FMath::IsNearlyEqual(ScanDuration, Other.ScanDuration);
}

UOutlierAbilitySystemComponent::UOutlierAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOutlierAbilitySystemComponent::InitializeForPawn(APawn* Pawn)
{
	if (!ensure(Pawn) || GetOwner() != Pawn)
	{
		return;
	}

	if (GetOwnerActor() != Pawn || GetAvatarActor() != Pawn)
	{
		InitAbilityActorInfo(Pawn, Pawn);
	}
	else
	{
		RefreshAbilityActorInfo();
	}

	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[GAS.Init] Actor=%s Owner=%s Avatar=%s Role=%d RepMode=%d"),
		*GetNameSafe(Pawn),
		*GetNameSafe(GetOwnerActor()),
		*GetNameSafe(GetAvatarActor()),
		static_cast<int32>(Pawn->GetLocalRole()),
		static_cast<int32>(ReplicationMode));
}

void UOutlierAbilitySystemComponent::ClearForPawn(const APawn* Pawn)
{
	if (GetOwnerActor() == Pawn || GetAvatarActor() == Pawn)
	{
		ClearActorInfo();
	}
}

bool UOutlierAbilitySystemComponent::ApplyDamageToSelf(
	float DamageAmount,
	AController* Instigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (!IsOwnerActorAuthoritative() || DamageAmount <= 0.0f
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune()))
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDamageGameplayEffect::StaticClass(),
		Instigator,
		DamageCauser);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Damage(), DamageAmount);
	if (DamageTag.IsValid())
	{
		SpecHandle.Data->AddDynamicAssetTag(DamageTag);
	}
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyShieldRecoveryToSelf(float Amount)
{
	if (!IsOwnerActorAuthoritative() || Amount <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierShieldRecoveryGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::ShieldRecovery(), Amount);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyPartnerShieldDeltaToSelf(
	float PartnerShieldDelta,
	float MaxPartnerShieldDelta)
{
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierPartnerShieldGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(
		OutlierGameplayTags::Data::PartnerShield(),
		PartnerShieldDelta);
	SpecHandle.Data->SetSetByCallerMagnitude(
		OutlierGameplayTags::Data::MaxPartnerShield(),
		MaxPartnerShieldDelta);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::ApplyDeadStateToSelf()
{
	if (!IsOwnerActorAuthoritative() || HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDeadGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::InitializeVitalityToSelf(float MaxHealth)
{
	if (!IsOwnerActorAuthoritative() || MaxHealth <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierVitalityInitializationGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::MaxHealth(), MaxHealth);
	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Health(), MaxHealth);
	return ApplySpecToSelf(*this, SpecHandle);
}

bool UOutlierAbilitySystemComponent::RestoreHealthToMax()
{
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}

	const float MaxHealth = GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute());
	if (MaxHealth <= 0.0f)
	{
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierHealthRecoveryGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(OutlierGameplayTags::Data::Health(), MaxHealth);
	return ApplySpecToSelf(*this, SpecHandle);
}

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyRebootStateToSelf(float DurationSeconds)
{
	if (!IsOwnerActorAuthoritative() || DurationSeconds <= 0.0f)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierRebootGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	SpecHandle.Data->SetDuration(DurationSeconds, true);
	return ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyDamageImmuneStateToSelf()
{
	if (!IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierDamageImmuneGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	return SpecHandle.IsValid()
		? ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())
		: FActiveGameplayEffectHandle();
}

bool UOutlierAbilitySystemComponent::RemoveActiveEffectFromSelf(FActiveGameplayEffectHandle Handle)
{
	return IsOwnerActorAuthoritative() && Handle.IsValid() && RemoveActiveGameplayEffect(Handle);
}

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::CommitTimedCooldown(
	TSubclassOf<UGameplayEffect> EffectClass,
	const FGameplayTag& CooldownTag,
	float DurationSeconds,
	UObject* SourceObject)
{
	if (!IsOwnerActorAuthoritative()
		|| !EffectClass
		|| !CooldownTag.IsValid()
		|| DurationSeconds <= 0.0f
		|| IsTimedCooldownActive(CooldownTag, SourceObject))
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(SourceObject ? SourceObject : GetAvatarActor());
	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	SpecHandle.Data->SetDuration(DurationSeconds, true);
	return ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

bool UOutlierAbilitySystemComponent::IsTimedCooldownActive(
	const FGameplayTag& CooldownTag,
	const UObject* SourceObject) const
{
	return GetTimedCooldownRemaining(CooldownTag, SourceObject) > 0.0f;
}

float UOutlierAbilitySystemComponent::GetTimedCooldownRemaining(
	const FGameplayTag& CooldownTag,
	const UObject* SourceObject) const
{
	if (!CooldownTag.IsValid())
	{
		return 0.0f;
	}

	float MaxRemaining = 0.0f;
	for (const FActiveGameplayEffectHandle& Handle : GetActiveEffects(MakeOwningTagQuery(CooldownTag)))
	{
		const FActiveGameplayEffect* ActiveEffect = GetActiveGameplayEffect(Handle);
		if (!ActiveEffect
			|| (SourceObject && ActiveEffect->Spec.GetContext().GetSourceObject() != SourceObject))
		{
			continue;
		}

		MaxRemaining = FMath::Max(MaxRemaining, ActiveEffect->GetTimeRemaining(GetWorld()->GetTimeSeconds()));
	}
	return MaxRemaining;
}

bool UOutlierAbilitySystemComponent::ConfigurePartnerAbilities(
	const FOutlierPartnerAbilityConfig& Config)
{
	FString Error;
	if (!Config.IsValid(Error))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(LogOutlier, Error, TEXT("[GAS.PartnerAbility] Invalid configuration: %s"), *Error);
		return false;
#else
		checkf(false, TEXT("[GAS.PartnerAbility] Invalid configuration: %s"), *Error);
		return false;
#endif
	}

	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}

	if (bPartnerAbilitiesConfigured)
	{
#if UE_BUILD_SHIPPING
		if (!PartnerAbilityConfig.Equals(Config))
		{
			UE_LOG(LogOutlier, Error, TEXT("[GAS.PartnerAbility] Configuration changed after grants"));
			return false;
		}
#else
		checkf(
			PartnerAbilityConfig.Equals(Config),
			TEXT("[GAS.PartnerAbility] Configuration changed after grants"));
#endif
		return true;
	}

	PartnerAbilityConfig = Config;
	const TSubclassOf<UGameplayAbility> AbilityClasses[] =
	{
		UOutlierPartnerEMPAbility::StaticClass(),
		UOutlierPartnerShieldAbility::StaticClass(),
		UOutlierPartnerHackAbility::StaticClass(),
		UOutlierPartnerScanAbility::StaticClass()
	};
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		int32 ExistingCount = 0;
		for (const FGameplayAbilitySpec& ExistingSpec : GetActivatableAbilities())
		{
			ExistingCount += ExistingSpec.Ability
				&& ExistingSpec.Ability->GetClass() == AbilityClass;
		}
		if (ExistingCount > 0)
		{
#if UE_BUILD_SHIPPING
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[GAS.PartnerAbility] Ability %s was already granted before configuration"),
				*GetNameSafe(AbilityClass));
			return false;
#else
			checkf(
				false,
				TEXT("[GAS.PartnerAbility] Ability %s was already granted before configuration"),
				*GetNameSafe(AbilityClass));
			return false;
#endif
		}
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		const FGameplayAbilitySpecHandle Handle = GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, GetOwnerActor()));
		if (!Handle.IsValid())
		{
			for (const FGameplayAbilitySpecHandle& GrantedHandle : GrantedPartnerAbilityHandles)
			{
				ClearAbility(GrantedHandle);
			}
			GrantedPartnerAbilityHandles.Reset();
			return false;
		}
		GrantedPartnerAbilityHandles.Add(Handle);
	}

	bPartnerAbilitiesConfigured = true;
	return true;
}

bool UOutlierAbilitySystemComponent::TryActivatePartnerAbility(const FGameplayTag& AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return false;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return TryActivateAbilitiesByTag(AbilityTags, true);
}

int32 UOutlierAbilitySystemComponent::GetGrantedPartnerAbilityCount() const
{
	int32 Count = 0;
	for (const FGameplayAbilitySpecHandle& Handle : GrantedPartnerAbilityHandles)
	{
		if (FindAbilitySpecFromHandle(Handle))
		{
			++Count;
		}
	}
	return Count;
}

bool UOutlierAbilitySystemComponent::CommitPartnerCooldown(
	const FGameplayTag& CooldownTag,
	float OverrideDuration)
{
	if (!IsOwnerActorAuthoritative()
		|| bPartnerSkillCooldownsSuspended
		|| IsPartnerCooldownActive(CooldownTag))
	{
		return false;
	}

	const float Duration = OverrideDuration > 0.0f
		? OverrideDuration
		: ResolvePartnerCooldownDuration(CooldownTag);
	const TSubclassOf<UGameplayEffect> EffectClass = ResolvePartnerCooldownEffectClass(CooldownTag);
	if (Duration <= 0.0f || !EffectClass)
	{
		return false;
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(GetAvatarActor());
	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	SpecHandle.Data->SetDuration(Duration, true);
	return ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get()).WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsPartnerCooldownActive(const FGameplayTag& CooldownTag) const
{
	return CooldownTag.IsValid() && GetActiveEffects(MakeOwningTagQuery(CooldownTag)).Num() > 0;
}

float UOutlierAbilitySystemComponent::GetPartnerCooldownRemaining(const FGameplayTag& CooldownTag) const
{
	if (!CooldownTag.IsValid())
	{
		return 0.0f;
	}

	const TArray<float> RemainingTimes = GetActiveEffectsTimeRemaining(MakeOwningTagQuery(CooldownTag));
	float MaxRemaining = 0.0f;
	for (const float Remaining : RemainingTimes)
	{
		MaxRemaining = FMath::Max(MaxRemaining, Remaining);
	}
	return MaxRemaining;
}

bool UOutlierAbilitySystemComponent::SuspendPartnerSkillCooldownsForPossession()
{
	if (!IsOwnerActorAuthoritative() || bPartnerSkillCooldownsSuspended)
	{
		return false;
	}

	bPartnerSkillCooldownsSuspended = true;
	SuspendedPartnerCooldowns.Reset();
	const FGameplayTag CooldownTags[] =
	{
		OutlierGameplayTags::Cooldown::Partner::EMP(),
		OutlierGameplayTags::Cooldown::Partner::Shield(),
		OutlierGameplayTags::Cooldown::Partner::Scan()
	};

	for (const FGameplayTag& CooldownTag : CooldownTags)
	{
		const float Remaining = GetPartnerCooldownRemaining(CooldownTag);
		if (Remaining <= 0.0f)
		{
			continue;
		}

		SuspendedPartnerCooldowns.Add(CooldownTag, Remaining);
		for (const FActiveGameplayEffectHandle& Handle : GetActiveEffects(MakeOwningTagQuery(CooldownTag)))
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	return true;
}

bool UOutlierAbilitySystemComponent::ResumePartnerSkillCooldownsAfterPossession()
{
	if (!IsOwnerActorAuthoritative() || !bPartnerSkillCooldownsSuspended)
	{
		return false;
	}

	bPartnerSkillCooldownsSuspended = false;
	const TMap<FGameplayTag, float> CooldownsToResume = MoveTemp(SuspendedPartnerCooldowns);
	SuspendedPartnerCooldowns.Reset();
	bool bAllResumed = true;
	for (const TPair<FGameplayTag, float>& Cooldown : CooldownsToResume)
	{
		if (!CommitPartnerCooldown(Cooldown.Key, Cooldown.Value))
		{
			bAllResumed = false;
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[GAS.PartnerAbility] Failed to resume cooldown %s with %.3f seconds"),
				*Cooldown.Key.ToString(),
				Cooldown.Value);
		}
	}
#if !UE_BUILD_SHIPPING
	checkf(bAllResumed, TEXT("[GAS.PartnerAbility] Failed to resume one or more suspended cooldowns"));
#endif
	return bAllResumed;
}

void UOutlierAbilitySystemComponent::DiscardSuspendedPartnerSkillCooldowns()
{
	bPartnerSkillCooldownsSuspended = false;
	SuspendedPartnerCooldowns.Reset();
}

float UOutlierAbilitySystemComponent::GetSuspendedPartnerCooldownRemaining(
	const FGameplayTag& CooldownTag) const
{
	const float* Remaining = SuspendedPartnerCooldowns.Find(CooldownTag);
	return Remaining ? *Remaining : 0.0f;
}

void UOutlierAbilitySystemComponent::CancelActivePartnerAbilities()
{
	FGameplayTagContainer PartnerAbilityTags;
	PartnerAbilityTags.AddTag(OutlierGameplayTags::Ability::Partner::EMP());
	PartnerAbilityTags.AddTag(OutlierGameplayTags::Ability::Partner::Shield());
	PartnerAbilityTags.AddTag(OutlierGameplayTags::Ability::Partner::Hacking());
	PartnerAbilityTags.AddTag(OutlierGameplayTags::Ability::Partner::Scan());
	CancelAbilities(&PartnerAbilityTags);
}

float UOutlierAbilitySystemComponent::ResolvePartnerCooldownDuration(const FGameplayTag& CooldownTag) const
{
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::EMP())
	{
		return PartnerAbilityConfig.EMPCooldown;
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Shield())
	{
		return PartnerAbilityConfig.ShieldCooldown;
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Hacking())
	{
		return PartnerAbilityConfig.HackCooldown;
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Scan())
	{
		return PartnerAbilityConfig.ScanCooldown;
	}
	return 0.0f;
}

TSubclassOf<UGameplayEffect> UOutlierAbilitySystemComponent::ResolvePartnerCooldownEffectClass(
	const FGameplayTag& CooldownTag) const
{
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::EMP())
	{
		return UOutlierPartnerEMPCooldownGameplayEffect::StaticClass();
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Shield())
	{
		return UOutlierPartnerShieldCooldownGameplayEffect::StaticClass();
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Hacking())
	{
		return UOutlierPartnerHackCooldownGameplayEffect::StaticClass();
	}
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Scan())
	{
		return UOutlierPartnerScanCooldownGameplayEffect::StaticClass();
	}
	return nullptr;
}
