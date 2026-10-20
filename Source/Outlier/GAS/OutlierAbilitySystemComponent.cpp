#include "GAS/OutlierAbilitySystemComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Effects/OutlierGameplayEffects.h"
#include "GAS/Abilities/Partner/OutlierPartnerGameplayAbilities.h"
#include "GAS/Abilities/Shooter/OutlierShooterGameplayAbilities.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Engine/World.h"
#include "Outlier.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Shooter/ShooterCharacter.h"

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

UOutlierAbilitySystemComponent::UOutlierAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOutlierAbilitySystemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UOutlierAbilitySystemComponent, QuantumLeapCharges, COND_OwnerOnly);
}


void UOutlierAbilitySystemComponent::InitializeForActor(AActor* Actor)
{
	if (!ensure(Actor) || GetOwner() != Actor)
	{
		return;
	}

	if (GetOwnerActor() != Actor || GetAvatarActor() != Actor)
	{
		InitAbilityActorInfo(Actor, Actor);
	}
	else
	{
		RefreshAbilityActorInfo();
	}

	if (IsOwnerActorAuthoritative())
	{
		if (!QuantumLeapChargeUpgradeHandle.IsValid())
		{
			QuantumLeapChargeUpgradeHandle = RegisterGameplayTagEvent(
				OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::DoubleCharge(), EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &ThisClass::HandleQuantumLeapChargeUpgrade);
		}
		RefreshShooterQuantumLeapCharges();
	}

	// 은신 비주얼은 State.Stealthed 태그를 보고 서브시스템이 전담한다. 여기서 구독만 걸어준다.
	if (UWorld* World = GetWorld())
	{
		if (UMaterialPostProcessSubsystem* MaterialSubsystem =
			World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			MaterialSubsystem->RegisterStealthSource(this);
		}
	}
}

void UOutlierAbilitySystemComponent::InitializeForPawn(APawn* Pawn)
{
	InitializeForActor(Pawn);
}

void UOutlierAbilitySystemComponent::ClearForActor(const AActor* Actor)
{
	if (GetOwnerActor() == Actor || GetAvatarActor() == Actor)
	{
		RegisterGameplayTagEvent(OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::DoubleCharge())
			.Remove(QuantumLeapChargeUpgradeHandle);
		QuantumLeapChargeUpgradeHandle.Reset();
		ResetShooterStealthUpgradeState();
		ResetShooterQuantumLeapTransientState();
		if (UWorld* World = GetWorld())
		{
			if (UMaterialPostProcessSubsystem* MaterialSubsystem =
				World->GetSubsystem<UMaterialPostProcessSubsystem>())
			{
				MaterialSubsystem->UnregisterStealthSource(this);
			}
		}

		ClearActorInfo();
	}
}

void UOutlierAbilitySystemComponent::ClearForPawn(const APawn* Pawn)
{
	ClearForActor(Pawn);
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

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyStunStateToSelf(
	float DurationSeconds,
	UObject* SourceObject)
{
	return ApplyTimedGameplayEffectToSelf(
		UOutlierStunGameplayEffect::StaticClass(),
		DurationSeconds,
		SourceObject);
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

FActiveGameplayEffectHandle UOutlierAbilitySystemComponent::ApplyPossessPendingStateToSelf()
{
	if (!IsOwnerActorAuthoritative()
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::PossessPending()))
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectSpecHandle SpecHandle = MakeSelfEffectSpec(
		*this,
		UOutlierPossessPendingGameplayEffect::StaticClass(),
		nullptr,
		GetAvatarActor());
	if (SpecHandle.IsValid())
	{
		// PossessPending belongs to this exact runtime instance, so the tag and
		// removable effect handle are created from the same authoritative spec.
		SpecHandle.Data->DynamicGrantedTags.AddTag(
			OutlierGameplayTags::State::PossessPending());
	}
	FActiveGameplayEffectHandle AppliedHandle = SpecHandle.IsValid()
		? ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())
		: FActiveGameplayEffectHandle();
	const FActiveGameplayEffect* ActiveEffect = GetActiveGameplayEffect(AppliedHandle);
	if (ActiveEffect && ActiveEffect->bIsInhibited)
	{
		// Possession gating must be visible before BeginPossessionProcess returns;
		// explicitly activate a newly stored GE if GAS left it initially inhibited.
		AppliedHandle = SetActiveGameplayEffectInhibit(MoveTemp(AppliedHandle), false, true);
	}
	return AppliedHandle;
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
		// 비교 대상은 "지금 적용중인 값"이 아니라 DT 기준 base 다.
		// PartnerAbilityConfig 는 업그레이드 투영( UpdatePartnerAbilityConfig )이 런타임에 덮어쓰므로,
		// 그걸로 비교하면 노드를 하나라도 찍은 뒤 재초기화가 들어올 때 항상 불일치로 터진다.
#if UE_BUILD_SHIPPING
		if (!BasePartnerAbilityConfig.Equals(Config))
		{
			UE_LOG(LogOutlier, Error, TEXT("[GAS.PartnerAbility] Configuration changed after grants"));
			return false;
		}
#else
		checkf(
			BasePartnerAbilityConfig.Equals(Config),
			TEXT("[GAS.PartnerAbility] Configuration changed after grants"));
#endif
		return true;
	}

	BasePartnerAbilityConfig = Config;
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

bool UOutlierAbilitySystemComponent::UpdatePartnerAbilityConfig(const FOutlierPartnerAbilityConfig& Config)
{
	if (!IsOwnerActorAuthoritative() || !bPartnerAbilitiesConfigured)
	{
		return false;
	}

	FString Error;
	if (!Config.IsValid(Error))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(LogOutlier, Error, TEXT("[GAS.PartnerAbility] Invalid runtime config: %s"), *Error);
		return false;
#else
		checkf(false, TEXT("[GAS.PartnerAbility] Invalid runtime config: %s"), *Error);
		return false;
#endif
	}

	// 능력은 이미 grant 된 상태. 값만 교체하면 다음 발동부터 반영된다.
	PartnerAbilityConfig = Config;
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
		// Partner 쪽과 같은 이유로 base 와 비교한다 ( UpdateShooterSuitConfig 가 런타임에 덮어씀 ).
#if UE_BUILD_SHIPPING
		return BaseShooterSuitConfig.Equals(Config);
#else
		checkf(BaseShooterSuitConfig.Equals(Config), TEXT("[GAS.ShooterSuit] Configuration changed after grant"));
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
	BaseShooterSuitConfig = Config;
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
	RefreshShooterQuantumLeapCharges();
	return true;
}

bool UOutlierAbilitySystemComponent::TryActivateShooterSuitAbility(const FGameplayTag& AbilityTag)
{
	if (!bShooterSuitConfigured || !AbilityTag.IsValid())
	{
		return false;
	}

	const FGameplayAbilitySpec* StealthSpec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);

	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::Stealth())
		&& StealthSpec
		&& StealthSpec->IsActive())
	{
		const UOutlierShooterStealthAbility* Stealth = Cast<UOutlierShooterStealthAbility>(StealthSpec->GetPrimaryInstance());
		if (Stealth && Stealth->IsAutomaticStealth())
		{
			return false;
		}
		// 같은 은신 입력은 새 Ability 재활성화가 아니라 플레이어의 명시적 취소로 처리한다.
		return EndActiveShooterStealth(true);
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return TryActivateAbilitiesByTag(AbilityTags, true);
}

bool UOutlierAbilitySystemComponent::IsShooterSuitAbilityUpgradeGrantRequired(
	const FGameplayTag& AbilityTag) const
{
	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::QuantumLeap()))
	{
		return bQuantumLeapRequiresUpgradeGrant;
	}
	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::BulletReflection()))
	{
		return bBulletReflectionRequiresUpgradeGrant;
	}
	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::WeaponOvercharge()))
	{
		return bWeaponOverchargeRequiresUpgradeGrant;
	}
	if (AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::Stealth()))
	{
		return bStealthRequiresUpgradeGrant;
	}

	return false;
}

bool UOutlierAbilitySystemComponent::IsShooterSuitAbilityGranted(
	const FGameplayTag& AbilityTag,
	bool bHasGrantTag) const
{
	// 그랜트가 필요 없는 능력(false)은 항상 통과, 필요한 능력(true)은 근거 태그가 있어야 통과.
	if (!IsShooterSuitAbilityUpgradeGrantRequired(AbilityTag) || bHasGrantTag)
	{
		return true;
	}

#if UE_BUILD_SHIPPING
	return false;
#else
	// 테스트 전용 마스터 스위치( bNoGrantMode, 기본 true ): 켜져 있으면 업그레이드 트리를 하나도 안
	// 찍었어도 Shooter 슈트 능력을 전부 바로 테스트할 수 있도록 무조건 통과.
	// 꺼두면 에디터/개발 빌드에서도 Shipping 과 완전히 같은 판정을 그대로 탄다.
	return bNoGrantMode;
#endif
}

FGameplayTag UOutlierAbilitySystemComponent::GetDefaultShooterSuitAbilityTag() const
{
	// 기본 제공 능력이 여러 개면 이 순서에서 앞쪽이 우선한다.
	const FGameplayTag AbilityTags[] =
	{
		OutlierGameplayTags::Ability::Shooter::QuantumLeap(),
		OutlierGameplayTags::Ability::Shooter::BulletReflection(),
		OutlierGameplayTags::Ability::Shooter::WeaponOvercharge(),
		OutlierGameplayTags::Ability::Shooter::Stealth(),
	};
	for (const FGameplayTag& AbilityTag : AbilityTags)
	{
		if (!IsShooterSuitAbilityUpgradeGrantRequired(AbilityTag))
		{
			return AbilityTag;
		}
	}

	return FGameplayTag();
}

bool UOutlierAbilitySystemComponent::UpdateShooterSuitConfig(const FOutlierShooterSuitConfig& Config)
{
	if (!IsOwnerActorAuthoritative() || !bShooterSuitConfigured)
	{
		return false;
	}

	FString Error;
	if (!Config.IsValid(Error))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(LogOutlier, Error, TEXT("[GAS.ShooterSuit] Invalid runtime config: %s"), *Error);
		return false;
#else
		checkf(false, TEXT("[GAS.ShooterSuit] Invalid runtime config: %s"), *Error);
		return false;
#endif
	}

	ShooterSuitConfig = Config;
	RefreshShooterQuantumLeapCharges();
	return true;
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

bool UOutlierAbilitySystemComponent::CommitShooterQuantumLeapCooldown()
{
	if (!IsOwnerActorAuthoritative() || !bShooterSuitConfigured
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}
	RefreshShooterQuantumLeapCharges();
	if (QuantumLeapCharges.CurrentCharges <= 0)
	{
		return false;
	}

	// 이동 성공 뒤에만 호출한다. 추가 사용은 횟수만 줄이고 진행 중인 충전 주기는 유지한다.
	--QuantumLeapCharges.CurrentCharges;
	BeginQuantumLeapRecharge();
	NotifyQuantumLeapChargesChanged(false);
	return true;
}

void UOutlierAbilitySystemComponent::RefreshShooterQuantumLeapCharges()
{
	if (!IsOwnerActorAuthoritative() || !bShooterSuitConfigured
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return;
	}
	const int32 DesiredMax = HasMatchingGameplayTag(
		OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::DoubleCharge()) ? 2 : 1;
	if (QuantumLeapCharges.bInitialized && QuantumLeapCharges.MaxCharges == DesiredMax)
	{
		return;
	}

	// 실제 최대치 증가분만 지급한다. 투영 GE를 교체하거나 같은 설정을 다시 적용해도 추가 지급하지 않는다.
	QuantumLeapCharges.CurrentCharges = QuantumLeapCharges.bInitialized
		? FMath::Clamp(QuantumLeapCharges.CurrentCharges + FMath::Max(0, DesiredMax - QuantumLeapCharges.MaxCharges), 0, DesiredMax)
		: DesiredMax;
	QuantumLeapCharges.MaxCharges = DesiredMax;
	QuantumLeapCharges.bInitialized = true;
	if (QuantumLeapCharges.CurrentCharges == DesiredMax)
	{
		StopQuantumLeapRecharge();
	}
	NotifyQuantumLeapChargesChanged(false);
}

void UOutlierAbilitySystemComponent::HandleQuantumLeapChargeUpgrade(const FGameplayTag Tag, int32 NewCount)
{
	RefreshShooterQuantumLeapCharges();
}

void UOutlierAbilitySystemComponent::BeginQuantumLeapRecharge()
{
	if (QuantumLeapRechargeHandle.IsValid() || QuantumLeapCharges.CurrentCharges >= QuantumLeapCharges.MaxCharges)
	{
		return;
	}
	const float Duration = ShooterSuitConfig.QuantumLeap.CooldownSeconds;
	if (Duration <= 0.0f)
	{
		// 0초 설정도 유효하다. 타이머 재귀 없이 부족한 횟수를 즉시 회복한다.
		QuantumLeapCharges.RecoverySerial += QuantumLeapCharges.MaxCharges - QuantumLeapCharges.CurrentCharges;
		QuantumLeapCharges.CurrentCharges = QuantumLeapCharges.MaxCharges;
		QuantumLeapCharges.RechargeDuration = 0.0f;
		QuantumLeapCharges.RechargeStartServerTime = 0.0f;
		NotifyQuantumLeapChargesChanged(true);
		return;
	}

	QuantumLeapRechargeHandle = ApplyTimedGameplayEffectToSelf(
		UOutlierShooterQuantumLeapCooldownGameplayEffect::StaticClass(), Duration, GetAvatarActor());
	if (!ensure(QuantumLeapRechargeHandle.IsValid()))
	{
		return;
	}
	QuantumLeapCharges.RechargeDuration = Duration;
	QuantumLeapCharges.RechargeStartServerTime = GetWorld()->GetTimeSeconds();
	if (FOnActiveGameplayEffectRemoved_Info* Removed = OnGameplayEffectRemoved_InfoDelegate(QuantumLeapRechargeHandle))
	{
		Removed->AddUObject(this, &ThisClass::HandleQuantumLeapRechargeRemoved, QuantumLeapRechargeHandle);
	}
}

void UOutlierAbilitySystemComponent::HandleQuantumLeapRechargeRemoved(
	const FGameplayEffectRemovalInfo& RemovalInfo, FActiveGameplayEffectHandle ExpectedHandle)
{
	if (QuantumLeapRechargeHandle != ExpectedHandle || !IsOwnerActorAuthoritative())
	{
		return;
	}
	QuantumLeapRechargeHandle.Invalidate();
	QuantumLeapCharges.RechargeDuration = 0.0f;
	QuantumLeapCharges.RechargeStartServerTime = 0.0f;
	if (RemovalInfo.bPrematureRemoval)
	{
		// 관리용 쿨타임 제거는 준비 상태로 복귀시키되, 자연 회복 연출로 전달하지 않는다.
		QuantumLeapCharges.CurrentCharges = QuantumLeapCharges.MaxCharges;
		NotifyQuantumLeapChargesChanged(false);
		return;
	}
	if (HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return;
	}
	QuantumLeapCharges.CurrentCharges = FMath::Min(QuantumLeapCharges.CurrentCharges + 1, QuantumLeapCharges.MaxCharges);
	++QuantumLeapCharges.RecoverySerial;
	// 만료된 한 주기만 회복하고, 아직 부족하면 현재 설정으로 다음 주기를 시작한다.
	BeginQuantumLeapRecharge();
	NotifyQuantumLeapChargesChanged(true);
}

void UOutlierAbilitySystemComponent::StopQuantumLeapRecharge()
{
	// 제거 콜백보다 먼저 소유권을 해제해 사망·종료 정리가 회복으로 처리되지 않게 한다.
	const FActiveGameplayEffectHandle PreviousHandle = QuantumLeapRechargeHandle;
	QuantumLeapRechargeHandle.Invalidate();
	RemoveActiveEffectFromSelf(PreviousHandle);
	QuantumLeapCharges.RechargeDuration = 0.0f;
	QuantumLeapCharges.RechargeStartServerTime = 0.0f;
}

void UOutlierAbilitySystemComponent::NotifyQuantumLeapChargesChanged(bool bRecovered)
{
	OnQuantumLeapChargesChanged.Broadcast(bRecovered);
	if (AActor* OwningActor = GetOwnerActor())
	{
		OwningActor->ForceNetUpdate();
	}
}

void UOutlierAbilitySystemComponent::OnRep_QuantumLeapCharges(const FOutlierQuantumLeapChargeState& PreviousState)
{
	// 최초 복제·최대치 변경은 회복이 아니다. 실제 회복 이력이 있고 횟수가 늘었을 때만 연출한다.
	const bool bRecovered = PreviousState.bInitialized && QuantumLeapCharges.bInitialized
		&& PreviousState.MaxCharges == QuantumLeapCharges.MaxCharges
		&& PreviousState.RecoverySerial != QuantumLeapCharges.RecoverySerial
		&& QuantumLeapCharges.CurrentCharges > PreviousState.CurrentCharges;
	OnQuantumLeapChargesChanged.Broadcast(bRecovered);
}

bool UOutlierAbilitySystemComponent::CommitShooterQuantumLeapRetryDelay()
{
	if (!IsOwnerActorAuthoritative() || HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return false;
	}
	if (IsShooterQuantumLeapRetryActive())
	{
		return true;
	}

	// 실패 제한은 성공 쿨타임과 별도 효과다. 진행 중인 재충전 효과를 교체하거나 연장하지 않는다.
	constexpr float RetryDelaySeconds = 0.5f;
	QuantumLeapRetryHandle = CommitTimedCooldown(
		UOutlierShooterQuantumLeapRetryGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry(),
		RetryDelaySeconds,
		GetAvatarActor());
	return QuantumLeapRetryHandle.WasSuccessfullyApplied();
}

bool UOutlierAbilitySystemComponent::IsShooterQuantumLeapRetryActive() const
{
	return IsTimedCooldownActive(OutlierGameplayTags::Cooldown::Shooter::QuantumLeapRetry(), GetAvatarActor());
}

void UOutlierAbilitySystemComponent::RefreshShooterQuantumLeapImmunity()
{
	if (!IsOwnerActorAuthoritative() || !bShooterSuitConfigured
		|| HasMatchingGameplayTag(OutlierGameplayTags::State::Dead())
		|| !HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::InvincibleAfterLeap()))
	{
		return;
	}

	const FActiveGameplayEffectHandle NewHandle = ApplyTimedGameplayEffectToSelf(
		UOutlierDamageImmuneGameplayEffect::StaticClass(), ShooterSuitConfig.QuantumLeap.DurationSeconds, GetAvatarActor());
	if (!NewHandle.IsValid())
	{
		return;
	}

	// 새 무적을 먼저 부여해 보호가 끊기지 않게 한다. 이전 도약 무적만 지워 시간을 새로 시작한다.
	const FActiveGameplayEffectHandle PreviousHandle = QuantumLeapImmunityHandle;
	QuantumLeapImmunityHandle = NewHandle;
	RemoveActiveEffectFromSelf(PreviousHandle);
}

void UOutlierAbilitySystemComponent::ResetShooterQuantumLeapTransientState()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}
	// 사망·Pawn 종료에서 도약 전용 효과와 충전 콜백을 정리한다. 다른 원인의 무적은 유지한다.
	StopQuantumLeapRecharge();
	QuantumLeapCharges.CurrentCharges = QuantumLeapCharges.MaxCharges;
	QuantumLeapCharges.bInitialized = false;
	RemoveActiveEffectFromSelf(QuantumLeapImmunityHandle);
	QuantumLeapImmunityHandle.Invalidate();
	RemoveActiveEffectFromSelf(QuantumLeapRetryHandle);
	QuantumLeapRetryHandle.Invalidate();
	NotifyQuantumLeapChargesChanged(false);
}

bool UOutlierAbilitySystemComponent::IsShooterQuantumLeapCooldownActive() const
{
	return GetShooterQuantumLeapCooldownRemaining() > 0.0f;
}

float UOutlierAbilitySystemComponent::GetShooterQuantumLeapCooldownRemaining() const
{
	const UWorld* World = GetWorld();
	if (!World || QuantumLeapCharges.RechargeDuration <= 0.0f)
	{
		return 0.0f;
	}
	const AGameStateBase* GameState = World->GetGameState();
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	return FMath::Clamp(QuantumLeapCharges.RechargeStartServerTime + QuantumLeapCharges.RechargeDuration - ServerTime,
		0.0f, QuantumLeapCharges.RechargeDuration);
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
	const float Duration = FMath::Max(0.0f, ShooterSuitConfig.Stealth.CooldownSeconds - PendingStealthCooldownReduction);
	PendingStealthCooldownReduction = 0.0f;
	if (Duration <= 0.0f)
	{
		return true;
	}
	return CommitTimedCooldown(
		UOutlierShooterStealthCooldownGameplayEffect::StaticClass(),
		OutlierGameplayTags::Cooldown::Shooter::Stealth(),
		Duration,
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
	if (!bCommitCooldown && IsOwnerActorAuthoritative())
	{
		ResetShooterStealthUpgradeState();
	}
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		return false;
	}
	UOutlierShooterStealthAbility* Ability = Cast<UOutlierShooterStealthAbility>(
		Spec->GetPrimaryInstance());
	return Ability && Ability->EndStealth(bCommitCooldown);
}

void UOutlierAbilitySystemComponent::BeginShooterStealthSession()
{
	ShooterStealthSession = MakeShared<FOutlierStealthSession>();
	ShooterStealthSession->Shooter = Cast<AShooterCharacter>(GetAvatarActor());
	ShooterStealthSession->Generation = StealthGeneration;
}

void UOutlierAbilitySystemComponent::FinishShooterStealthSession()
{
	// 이미 실행한 공격은 공유 자격을 보유한다. 현재 은신만 해제해 다음 공격에 재사용하지 않는다.
	ShooterStealthSession.Reset();
}

TSharedPtr<FOutlierStealthSession> UOutlierAbilitySystemComponent::BeginShooterStealthAttack()
{
	if (!IsOwnerActorAuthoritative())
	{
		return nullptr;
	}
	TSharedPtr<FOutlierStealthSession> Session = HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed())
		? ShooterStealthSession : nullptr;
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
	if (UOutlierShooterStealthAbility* Ability = Spec ? Cast<UOutlierShooterStealthAbility>(Spec->GetPrimaryInstance()) : nullptr)
	{
		Ability->NotifyOffensiveAction();
	}
	return Session;
}

void UOutlierAbilitySystemComponent::NotifyShooterStealthDamage()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}
	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
	if (UOutlierShooterStealthAbility* Ability = Spec ? Cast<UOutlierShooterStealthAbility>(Spec->GetPrimaryInstance()) : nullptr)
	{
		Ability->NotifyIncomingDamage();
	}
}

void UOutlierAbilitySystemComponent::NotifyShooterStealthKill(
	const TSharedPtr<FOutlierStealthSession>& Session, bool bDrone)
{
	if (!IsOwnerActorAuthoritative() || !Session || Session->Shooter.Get() != GetAvatarActor()
		|| Session->Generation != StealthGeneration || HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		return;
	}

	// 기존 은신의 첫 처치 보상을 먼저 소비한 뒤, 다섯 번째 처치의 새 은신을 발동한다.
	if (!Session->bAssassinsStrikeConsumed
		&& HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::AssassinsStrike()))
	{
		Session->bAssassinsStrikeConsumed = true;
		const float Remaining = GetShooterStealthCooldownRemaining();
		if (Remaining > 0.0f)
		{
			RemoveActiveEffects(MakeOwningTagQuery(OutlierGameplayTags::Cooldown::Shooter::Stealth()));
			if (Remaining > 5.0f)
			{
				CommitTimedCooldown(UOutlierShooterStealthCooldownGameplayEffect::StaticClass(),
					OutlierGameplayTags::Cooldown::Shooter::Stealth(), Remaining - 5.0f, GetAvatarActor());
			}
		}
		else
		{
			PendingStealthCooldownReduction += 5.0f;
		}
	}

	if (bDrone && HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ConcealmentProtocol()))
	{
		if (++StealthDroneKillCount >= 5)
		{
			StealthDroneKillCount -= 5;
			FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(GrantedShooterStealthAbilityHandle);
			if (UOutlierShooterStealthAbility* Ability = Spec ? Cast<UOutlierShooterStealthAbility>(Spec->GetPrimaryInstance()) : nullptr)
			{
				Ability->TriggerAutomaticStealth();
			}
		}
	}
}

void UOutlierAbilitySystemComponent::ApplyShooterStealthExitSpeedBoost()
{
	if (IsOwnerActorAuthoritative()
		&& HasMatchingGameplayTag(OutlierGameplayTags::Upgrade::Shooter::Stealth::ShadowStep())
		&& ShooterSuitConfig.Stealth.ExitSpeedBuffDurationSeconds > 0.0f
		&& !HasMatchingGameplayTag(OutlierGameplayTags::State::Dead()))
	{
		const FActiveGameplayEffectHandle Previous = StealthExitSpeedHandle;
		StealthExitSpeedHandle = ApplyTimedGameplayEffectToSelf(
			UOutlierShooterStealthExitSpeedGameplayEffect::StaticClass(),
			ShooterSuitConfig.Stealth.ExitSpeedBuffDurationSeconds, GetAvatarActor());
		RemoveActiveEffectFromSelf(Previous);
	}
}

void UOutlierAbilitySystemComponent::ResetShooterStealthUpgradeState()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}
	++StealthGeneration;
	ShooterStealthSession.Reset();
	StealthDroneKillCount = 0;
	PendingStealthCooldownReduction = 0.0f;
	RemoveActiveEffectFromSelf(StealthExitSpeedHandle);
	StealthExitSpeedHandle.Invalidate();
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
	if (!EffectClass)
	{
		// CooldownTag 가 알려진 Partner 쿨다운(EMP/Shield/Hacking/Scan) 에 매핑되지 않는 경우.
		// 이건 진짜 설정 오류이므로 실패로 취급한다.
		return false;
	}

	// 업그레이드 델타로 HackCooldown/EMPCooldown 등이 0까지 깎인 경우, Duration 이 0 이 될 수 있다.
	// 이 경우 걸어줄 GE 가 없을 뿐 "쿨다운 없음" 은 정상적인 성공 상태다.
	// 여기서 실패로 취급하면 CommitConfiguredCooldown() 의 checkf(bCommitted, ...) 가 크래시한다.
	if (Duration <= 0.0f)
	{
		return true;
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
