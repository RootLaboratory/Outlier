#include "GAS/Effects/OutlierGameplayEffects.h"

#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
FGameplayModifierInfo MakeSetByCallerModifier(
	const FGameplayAttribute& Attribute,
	const FGameplayTag& DataTag,
	EGameplayModOp::Type ModifierOp = EGameplayModOp::Additive)
{
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = DataTag;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = Attribute;
	Modifier.ModifierOp = ModifierOp;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	return Modifier;
}

}

UOutlierVitalityInitializationGameplayEffect::UOutlierVitalityInitializationGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierVitalAttributeSet::GetMaxHealthAttribute(),
		OutlierGameplayTags::Data::MaxHealth(),
		EGameplayModOp::Override));
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierVitalAttributeSet::GetHealthAttribute(),
		OutlierGameplayTags::Data::Health(),
		EGameplayModOp::Override));
}

UOutlierHealthRecoveryGameplayEffect::UOutlierHealthRecoveryGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierVitalAttributeSet::GetHealthAttribute(),
		OutlierGameplayTags::Data::Health(),
		EGameplayModOp::Override));
}

UOutlierRebootGameplayEffect::UOutlierRebootGameplayEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::Rebooting());
	GrantedTags.AddTag(OutlierGameplayTags::State::DamageImmune());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("TargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierStunGameplayEffect::UOutlierStunGameplayEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::Stunned());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("StunTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierDamageImmuneGameplayEffect::UOutlierDamageImmuneGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::DamageImmune());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("TargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierDamageGameplayEffect::UOutlierDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierVitalAttributeSet::GetIncomingDamageAttribute(),
		OutlierGameplayTags::Data::Damage()));
}

UOutlierShieldRecoveryGameplayEffect::UOutlierShieldRecoveryGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierShieldAttributeSet::GetShieldAttribute(),
		OutlierGameplayTags::Data::ShieldRecovery()));
}

UOutlierPartnerShieldGameplayEffect::UOutlierPartnerShieldGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(),
		OutlierGameplayTags::Data::MaxPartnerShield()));
	Modifiers.Add(MakeSetByCallerModifier(
		UOutlierShieldAttributeSet::GetPartnerShieldAttribute(),
		OutlierGameplayTags::Data::PartnerShield()));
}

UOutlierDeadGameplayEffect::UOutlierDeadGameplayEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::Dead());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("TargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierPossessPendingGameplayEffect::UOutlierPossessPendingGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}

UOutlierWeaponReuseCooldownGameplayEffect::UOutlierWeaponReuseCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::Cooldown::Weapon::Reuse());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("WeaponReuseCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterQuantumLeapCooldownGameplayEffect::UOutlierShooterQuantumLeapCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::QuantumLeap());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("QuantumLeapCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterBulletReflectionGameplayEffect::UOutlierShooterBulletReflectionGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::BulletReflecting());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("BulletReflectionTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterBulletReflectionCooldownGameplayEffect::UOutlierShooterBulletReflectionCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::BulletReflection());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("BulletReflectionCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterWeaponOverchargeGameplayEffect::UOutlierShooterWeaponOverchargeGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::WeaponOvercharged());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("WeaponOverchargeTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterWeaponOverchargeCooldownGameplayEffect::UOutlierShooterWeaponOverchargeCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("WeaponOverchargeCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterStealthGameplayEffect::UOutlierShooterStealthGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::State::Stealthed());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("StealthTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierShooterStealthCooldownGameplayEffect::UOutlierShooterStealthCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(OutlierGameplayTags::Cooldown::Shooter::Stealth());
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("StealthCooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierPartnerCooldownGameplayEffect::UOutlierPartnerCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
}

void UOutlierPartnerCooldownGameplayEffect::ConfigureCooldownTag(
	const FObjectInitializer& ObjectInitializer,
	const FGameplayTag& CooldownTag)
{
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(CooldownTag);
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("CooldownTargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}

UOutlierPartnerEMPCooldownGameplayEffect::UOutlierPartnerEMPCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigureCooldownTag(ObjectInitializer, OutlierGameplayTags::Cooldown::Partner::EMP());
}

UOutlierPartnerShieldCooldownGameplayEffect::UOutlierPartnerShieldCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigureCooldownTag(ObjectInitializer, OutlierGameplayTags::Cooldown::Partner::Shield());
}

UOutlierPartnerHackCooldownGameplayEffect::UOutlierPartnerHackCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigureCooldownTag(ObjectInitializer, OutlierGameplayTags::Cooldown::Partner::Hacking());
}

UOutlierPartnerScanCooldownGameplayEffect::UOutlierPartnerScanCooldownGameplayEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigureCooldownTag(ObjectInitializer, OutlierGameplayTags::Cooldown::Partner::Scan());
}
