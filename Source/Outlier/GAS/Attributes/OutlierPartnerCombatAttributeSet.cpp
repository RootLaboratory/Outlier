#include "GAS/Attributes/OutlierPartnerCombatAttributeSet.h"
#include "Net/UnrealNetwork.h"

UOutlierPartnerCombatAttributeSet::UOutlierPartnerCombatAttributeSet()
	: BasicAttackStunMultiplier(1.0f)
	, HackedDroneDamageMultiplier(1.0f)
{
}

void UOutlierPartnerCombatAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (Attribute == GetBasicAttackStunMultiplierAttribute() || Attribute == GetHackedDroneDamageMultiplierAttribute())
		NewValue = FMath::IsFinite(NewValue) ? FMath::Max(0.0f, NewValue) : 1.0f;
}

void UOutlierPartnerCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetBasicAttackStunMultiplierAttribute() || Attribute == GetHackedDroneDamageMultiplierAttribute())
		NewValue = FMath::IsFinite(NewValue) ? FMath::Max(0.0f, NewValue) : 1.0f;
}

void UOutlierPartnerCombatAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierPartnerCombatAttributeSet, BasicAttackStunMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierPartnerCombatAttributeSet, HackedDroneDamageMultiplier, COND_None, REPNOTIFY_Always);
}

void UOutlierPartnerCombatAttributeSet::OnRep_HackedDroneDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierPartnerCombatAttributeSet, HackedDroneDamageMultiplier, OldValue);
}

void UOutlierPartnerCombatAttributeSet::OnRep_BasicAttackStunMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierPartnerCombatAttributeSet, BasicAttackStunMultiplier, OldValue);
}
