#include "GAS/Effects/OutlierGameplayEffects.h"

#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

namespace
{
FGameplayModifierInfo MakeSetByCallerModifier(
	const FGameplayAttribute& Attribute,
	const FGameplayTag& DataTag)
{
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = DataTag;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = Attribute;
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	return Modifier;
}
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
