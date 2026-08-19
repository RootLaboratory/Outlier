#include "GAS/OutlierAbilitySystemComponent.h"

#include "GameFramework/Pawn.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"
#include "GAS/Abilities/Shooter/OutlierShooterGameplayAbilities.h"
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
	return ApplyShieldDeltaToSelf(Amount);
}

bool UOutlierAbilitySystemComponent::ApplyShieldDeltaToSelf(float Amount)
{
	if (!IsOwnerActorAuthoritative() || FMath::IsNearlyZero(Amount))
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

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyTimedGameplayEffectToSelf(
	TSubclassOf<UGameplayEffect> EffectClass,
	float DurationSeconds,
	UObject* SourceObject)
{
	if (!IsOwnerActorAuthoritative() || !EffectClass || DurationSeconds <= 0.0f)
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

bool UOutlierAbilitySystemComponent::ConfigureShooterSuitAbilities(
	const FOutlierShooterSuitConfig& Config)
{
	FString Error;
	if (!Config.IsValid(Error))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(LogOutlier, Error, TEXT("[GAS.ShooterSuit] Invalid configuration: %s"), *Error);
		return false;
#else
		checkf(false, TEXT("[GAS.ShooterSuit] Invalid configuration: %s"), *Error);
		return false;
#endif
	}
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}
	if (bShooterSuitConfigured)
	{
#if UE_BUILD_SHIPPING
		return ShooterSuitConfig.Equals(Config);
#else
		checkf(ShooterSuitConfig.Equals(Config), TEXT("[GAS.ShooterSuit] Configuration changed after grant"));
		return true;
#endif
	}

	for (const FGameplayAbilitySpec& ExistingSpec : GetActivatableAbilities())
	{
		if (ExistingSpec.Ability
			&& (ExistingSpec.Ability->GetClass() == UOutlierShooterQuantumLeapAbility::StaticClass()
				|| ExistingSpec.Ability->GetClass() == UOutlierShooterBulletReflectionAbility::StaticClass()
				|| ExistingSpec.Ability->GetClass() == UOutlierShooterWeaponOverchargeAbility::StaticClass()
				|| ExistingSpec.Ability->GetClass() == UOutlierShooterStealthAbility::StaticClass()))
		{
#if UE_BUILD_SHIPPING
			UE_LOG(LogOutlier, Error, TEXT("[GAS.ShooterSuit] A native suit ability was already granted before configuration"));
			return false;
#else
			checkf(false, TEXT("[GAS.ShooterSuit] A native suit ability was already granted before configuration"));
			return false;
#endif
		}
	}
	ShooterSuitConfig = Config;
	GrantedShooterQuantumLeapAbilityHandle = GiveAbility(FGameplayAbilitySpec(
		UOutlierShooterQuantumLeapAbility::StaticClass(), 1, INDEX_NONE, GetOwnerActor()));
	GrantedShooterBulletReflectionAbilityHandle = GiveAbility(FGameplayAbilitySpec(
		UOutlierShooterBulletReflectionAbility::StaticClass(), 1, INDEX_NONE, GetOwnerActor()));
	GrantedShooterWeaponOverchargeAbilityHandle = GiveAbility(FGameplayAbilitySpec(
		UOutlierShooterWeaponOverchargeAbility::StaticClass(), 1, INDEX_NONE, GetOwnerActor()));
	GrantedShooterStealthAbilityHandle = GiveAbility(FGameplayAbilitySpec(
		UOutlierShooterStealthAbility::StaticClass(), 1, INDEX_NONE, GetOwnerActor()));
	if (!GrantedShooterQuantumLeapAbilityHandle.IsValid()
		|| !GrantedShooterBulletReflectionAbilityHandle.IsValid()
		|| !GrantedShooterWeaponOverchargeAbilityHandle.IsValid()
		|| !GrantedShooterStealthAbilityHandle.IsValid())
	{
		if (GrantedShooterQuantumLeapAbilityHandle.IsValid())
		{
			ClearAbility(GrantedShooterQuantumLeapAbilityHandle);
			GrantedShooterQuantumLeapAbilityHandle = FGameplayAbilitySpecHandle();
		}
		if (GrantedShooterBulletReflectionAbilityHandle.IsValid())
		{
			ClearAbility(GrantedShooterBulletReflectionAbilityHandle);
			GrantedShooterBulletReflectionAbilityHandle = FGameplayAbilitySpecHandle();
		}
		if (GrantedShooterWeaponOverchargeAbilityHandle.IsValid())
		{
			ClearAbility(GrantedShooterWeaponOverchargeAbilityHandle);
			GrantedShooterWeaponOverchargeAbilityHandle = FGameplayAbilitySpecHandle();
		}
		if (GrantedShooterStealthAbilityHandle.IsValid())
		{
			ClearAbility(GrantedShooterStealthAbilityHandle);
			GrantedShooterStealthAbilityHandle = FGameplayAbilitySpecHandle();
		}
		return false;
	}
	bShooterSuitConfigured = true;
	return true;
}

bool UOutlierAbilitySystemComponent::TryActivateShooterSuitAbility(const FGameplayTag& AbilityTag)
{
	if (!bShooterSuitConfigured || !AbilityTag.IsValid())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[GAS.ShooterSuit.Trace] RequestRejected Owner=%s Ability=%s Configured=%d ValidTag=%d"),
			*GetNameSafe(GetAvatarActor()),
			*AbilityTag.ToString(),
			bShooterSuitConfigured ? 1 : 0,
			AbilityTag.IsValid() ? 1 : 0);
		return false;
	}

	const FGameplayAbilitySpec* QuantumLeapSpec = FindAbilitySpecFromHandle(GrantedShooterQuantumLeapAbilityHandle);
	const FGameplayAbilitySpec* BulletReflectionSpec = FindAbilitySpecFromHandle(GrantedShooterBulletReflectionAbilityHandle);
	const FGameplayAbilitySpec* WeaponOverchargeSpec = FindAbilitySpecFromHandle(GrantedShooterWeaponOverchargeAbilityHandle);
	const FGameplayAbilitySpec* StealthSpec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[GAS.ShooterSuit.Trace] Request Owner=%s Ability=%s Authority=%d QuantumActive=%d ReflectionActive=%d OverchargeActive=%d StealthActive=%d Stealthed=%d QuantumCooldown=%.2f ReflectionCooldown=%.2f OverchargeCooldown=%.2f StealthCooldown=%.2f"),
		*GetNameSafe(GetAvatarActor()),
		*AbilityTag.ToString(),
		IsOwnerActorAuthoritative() ? 1 : 0,
		QuantumLeapSpec && QuantumLeapSpec->IsActive() ? 1 : 0,
		BulletReflectionSpec && BulletReflectionSpec->IsActive() ? 1 : 0,
		WeaponOverchargeSpec && WeaponOverchargeSpec->IsActive() ? 1 : 0,
		StealthSpec && StealthSpec->IsActive() ? 1 : 0,
		HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()) ? 1 : 0,
		GetShooterQuantumLeapCooldownRemaining(),
		GetShooterBulletReflectionCooldownRemaining(),
		GetShooterWeaponOverchargeCooldownRemaining(),
		GetShooterStealthCooldownRemaining());

	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::Stealth())
		&& StealthSpec
		&& StealthSpec->IsActive())
	{
		// 같은 은신 입력은 새 Ability 재활성화가 아니라 플레이어의 명시적 취소로 처리한다.
		return EndActiveShooterStealth(true);
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	const bool bActivated = TryActivateAbilitiesByTag(AbilityTags, true);
	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[GAS.ShooterSuit.Trace] RequestResult Owner=%s Ability=%s Activated=%d QuantumActive=%d ReflectionActive=%d OverchargeActive=%d StealthActive=%d Stealthed=%d"),
		*GetNameSafe(GetAvatarActor()),
		*AbilityTag.ToString(),
		bActivated ? 1 : 0,
		QuantumLeapSpec && QuantumLeapSpec->IsActive() ? 1 : 0,
		BulletReflectionSpec && BulletReflectionSpec->IsActive() ? 1 : 0,
		WeaponOverchargeSpec && WeaponOverchargeSpec->IsActive() ? 1 : 0,
		StealthSpec && StealthSpec->IsActive() ? 1 : 0,
		HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed()) ? 1 : 0);
	return bActivated;
}

int32 UOutlierAbilitySystemComponent::GetGrantedShooterSuitAbilityCount() const
{
	int32 Count = 0;
	Count += FindAbilitySpecFromHandle(GrantedShooterQuantumLeapAbilityHandle) ? 1 : 0;
	Count += FindAbilitySpecFromHandle(GrantedShooterBulletReflectionAbilityHandle) ? 1 : 0;
	Count += FindAbilitySpecFromHandle(GrantedShooterWeaponOverchargeAbilityHandle) ? 1 : 0;
	Count += FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle) ? 1 : 0;
	return Count;
}

bool UOutlierAbilitySystemComponent::CommitShooterQuantumLeapCooldown(float DurationMultiplier)
{
	return DurationMultiplier > 0.0f && CommitTimedCooldown(
		UOutlierShooterQuantumLeapCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap(),
		ShooterSuitConfig.QuantumLeap.CooldownSeconds * DurationMultiplier,
		GetAvatarActor()).WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsShooterQuantumLeapCooldownActive() const
{
	return IsTimedCooldownActive(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap(),
		GetAvatarActor());
}

float UOutlierAbilitySystemComponent::GetShooterQuantumLeapCooldownRemaining() const
{
	return GetTimedCooldownRemaining(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap(),
		GetAvatarActor());
}

bool UOutlierAbilitySystemComponent::CancelActiveShooterQuantumLeap(bool bCommitFailureCooldown)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterQuantumLeapAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		return false;
	}
	UOutlierShooterQuantumLeapAbility* Ability = Cast<UOutlierShooterQuantumLeapAbility>(
		Spec->GetPrimaryInstance());
	return Ability && Ability->CancelQuantumLeap(bCommitFailureCooldown);
}

bool UOutlierAbilitySystemComponent::CommitShooterBulletReflectionCooldown()
{
	return CommitTimedCooldown(
		UOutlierShooterBulletReflectionCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::BulletReflection(),
		ShooterSuitConfig.BulletReflection.CooldownSeconds,
		GetAvatarActor()).WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsShooterBulletReflectionCooldownActive() const
{
	return IsTimedCooldownActive(
		OutlierGameplayTags::Cooldown::Shooter::BulletReflection(),
		GetAvatarActor());
}

float UOutlierAbilitySystemComponent::GetShooterBulletReflectionCooldownRemaining() const
{
	return GetTimedCooldownRemaining(
		OutlierGameplayTags::Cooldown::Shooter::BulletReflection(),
		GetAvatarActor());
}

bool UOutlierAbilitySystemComponent::EndActiveShooterBulletReflection(bool bCommitCooldown)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterBulletReflectionAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		return false;
	}
	UOutlierShooterBulletReflectionAbility* Ability = Cast<UOutlierShooterBulletReflectionAbility>(
		Spec->GetPrimaryInstance());
	return Ability && Ability->EndBulletReflection(bCommitCooldown);
}

bool UOutlierAbilitySystemComponent::CommitShooterWeaponOverchargeCooldown()
{
	return CommitTimedCooldown(
		UOutlierShooterWeaponOverchargeCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge(),
		ShooterSuitConfig.WeaponOvercharge.CooldownSeconds,
		GetAvatarActor()).WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsShooterWeaponOverchargeCooldownActive() const
{
	return IsTimedCooldownActive(
		OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge(),
		GetAvatarActor());
}

float UOutlierAbilitySystemComponent::GetShooterWeaponOverchargeCooldownRemaining() const
{
	return GetTimedCooldownRemaining(
		OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge(),
		GetAvatarActor());
}

bool UOutlierAbilitySystemComponent::EndActiveShooterWeaponOvercharge(bool bCommitCooldown)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterWeaponOverchargeAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		return false;
	}
	UOutlierShooterWeaponOverchargeAbility* Ability = Cast<UOutlierShooterWeaponOverchargeAbility>(
		Spec->GetPrimaryInstance());
	return Ability && Ability->EndWeaponOvercharge(bCommitCooldown);
}

bool UOutlierAbilitySystemComponent::CommitShooterStealthCooldown()
{
	return CommitTimedCooldown(
		UOutlierShooterStealthCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::Stealth(),
		ShooterSuitConfig.Stealth.CooldownSeconds,
		GetAvatarActor()).WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsShooterStealthCooldownActive() const
{
	return IsTimedCooldownActive(
		OutlierGameplayTags::Cooldown::Shooter::Stealth(),
		GetAvatarActor());
}

float UOutlierAbilitySystemComponent::GetShooterStealthCooldownRemaining() const
{
	return GetTimedCooldownRemaining(
		OutlierGameplayTags::Cooldown::Shooter::Stealth(),
		GetAvatarActor());
}

bool UOutlierAbilitySystemComponent::EndActiveShooterStealth(bool bCommitCooldown)
{
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		return false;
	}
	UOutlierShooterStealthAbility* Ability = Cast<UOutlierShooterStealthAbility>(
		Spec->GetPrimaryInstance());
	return Ability && Ability->EndStealth(bCommitCooldown);
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
