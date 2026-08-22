#include "GAS/Attributes/OutlierVitalAttributeSet.h"

#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UOutlierVitalAttributeSet::UOutlierVitalAttributeSet()
	: Health(100.0f)
	, MaxHealth(100.0f)
	, IncomingDamage(0.0f)
{
}

void UOutlierVitalAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute != GetIncomingDamageAttribute())
	{
		return;
	}

	const float Damage = FMath::Max(GetIncomingDamage(), 0.0f);
	SetIncomingDamage(0.0f);
	if (Damage <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return;
	}

	float RemainingDamage = Damage;
	if (const UOutlierShieldAttributeSet* ShieldSet = AbilitySystem->GetSet<UOutlierShieldAttributeSet>())
	{
		const float PartnerAbsorption = FMath::Min(ShieldSet->GetPartnerShield(), RemainingDamage);
		if (PartnerAbsorption > 0.0f)
		{
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetPartnerShieldAttribute(),
				ShieldSet->GetPartnerShield() - PartnerAbsorption);
			RemainingDamage -= PartnerAbsorption;
		}

		const float ShieldAbsorption = FMath::Min(ShieldSet->GetShield(), RemainingDamage);
		if (ShieldAbsorption > 0.0f)
		{
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetShieldAttribute(),
				ShieldSet->GetShield() - ShieldAbsorption);
			RemainingDamage -= ShieldAbsorption;
		}
	}

	if (RemainingDamage > 0.0f)
	{
		AbilitySystem->SetNumericAttributeBase(
			GetHealthAttribute(),
			FMath::Max(GetHealth() - RemainingDamage, 0.0f));
	}
}

void UOutlierVitalAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute,
	float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
		if (GetHealth() > NewValue)
		{
			if (UAbilitySystemComponent* AbilitySystem = GetOwningAbilitySystemComponent())
			{
				AbilitySystem->SetNumericAttributeBase(GetHealthAttribute(), NewValue);
			}
		}
	}
	else if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
}

void UOutlierVitalAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(
		UOutlierVitalAttributeSet,
		Health,
		COND_None,
		REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UOutlierVitalAttributeSet,
		MaxHealth,
		COND_None,
		REPNOTIFY_Always);
}

void UOutlierVitalAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierVitalAttributeSet, Health, OldHealth);
}

void UOutlierVitalAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierVitalAttributeSet, MaxHealth, OldMaxHealth);
}
