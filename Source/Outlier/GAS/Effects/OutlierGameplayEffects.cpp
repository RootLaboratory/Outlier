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
