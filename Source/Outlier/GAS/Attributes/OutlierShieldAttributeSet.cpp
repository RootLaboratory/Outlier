#include "GAS/Attributes/OutlierShieldAttributeSet.h"

#include "Net/UnrealNetwork.h"

UOutlierShieldAttributeSet::UOutlierShieldAttributeSet()
	: Shield(100.0f)
	, MaxShield(100.0f)
	, PartnerShield(0.0f)
	, MaxPartnerShield(0.0f)
{
}

void UOutlierShieldAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute,
	float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMaxShieldAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
		if (GetShield() > NewValue)
		{
			if (UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent())
			{
				AbilitySystem->SetNumericAttributeBase(GetShieldAttribute(), NewValue);
			}
		}
	}
	else if (Attribute == GetMaxPartnerShieldAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
		if (GetPartnerShield() > NewValue)
		{
			if (UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent())
			{
				AbilitySystem->SetNumericAttributeBase(GetPartnerShieldAttribute(), NewValue);
			}
		}
	}
	else if (Attribute == GetShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxShield());
	}
	else if (Attribute == GetPartnerShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxPartnerShield());
	}
}

void UOutlierShieldAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierShieldAttributeSet, Shield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierShieldAttributeSet, MaxShield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierShieldAttributeSet, PartnerShield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UOutlierShieldAttributeSet, MaxPartnerShield, COND_None, REPNOTIFY_Always);
}

void UOutlierShieldAttributeSet::OnRep_Shield(const FGameplayAttributeData& OldShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierShieldAttributeSet, Shield, OldShield);
}

void UOutlierShieldAttributeSet::OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierShieldAttributeSet, MaxShield, OldMaxShield);
}

void UOutlierShieldAttributeSet::OnRep_PartnerShield(const FGameplayAttributeData& OldPartnerShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierShieldAttributeSet, PartnerShield, OldPartnerShield);
}

void UOutlierShieldAttributeSet::OnRep_MaxPartnerShield(
	const FGameplayAttributeData& OldMaxPartnerShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(
		UOutlierShieldAttributeSet,
		MaxPartnerShield,
		OldMaxPartnerShield);
}
