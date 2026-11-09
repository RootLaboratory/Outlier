#include "GAS/Attributes/OutlierVitalAttributeSet.h"

#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Damage/OutlierDamageReceiver.h"
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

	LastHealthDamage = 0.0f;
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
	// IncomingDamage를 직접 적용하는 GE도 실드/체력 소비 전에 같은 보호를 확인한다.
	if (OutlierDamage::IsEditorInvulnerable(AbilitySystem->GetAvatarActor()))
	{
		return;
	}

	float RemainingDamage = Damage;
	const bool bInstantKill = Data.EffectSpec.GetDynamicAssetTags().HasTagExact(OutlierDamage::InstantKillTag());
	if (const UOutlierShieldAttributeSet* ShieldSet = AbilitySystem->GetSet<UOutlierShieldAttributeSet>())
	{
		// 즉사도 일반 피해와 같은 실드 → 체력 순서로 처리한다. 큰 수의 합산/반올림에 의존하지 않는다.
		const float PartnerAbsorption = bInstantKill ? ShieldSet->GetPartnerShield()
			: FMath::Min(ShieldSet->GetPartnerShield(), RemainingDamage);
		if (PartnerAbsorption > 0.0f)
		{
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetPartnerShieldAttribute(),
				ShieldSet->GetPartnerShield() - PartnerAbsorption);
			RemainingDamage -= PartnerAbsorption;
		}

		const float ShieldAbsorption = bInstantKill ? ShieldSet->GetShield()
			: FMath::Min(ShieldSet->GetShield(), RemainingDamage);
		if (ShieldAbsorption > 0.0f)
		{
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetShieldAttribute(),
				ShieldSet->GetShield() - ShieldAbsorption);
			RemainingDamage -= ShieldAbsorption;
		}
	}

	if (bInstantKill || RemainingDamage > 0.0f)
	{
		// UI 피드백 단계는 실드를 넘어 Health 에 도달한 양으로 정한다. 즉사는 남은 Health 전체를 기록한다.
		LastHealthDamage = bInstantKill ? FMath::Max(GetHealth(), 0.0f) : RemainingDamage;
		AbilitySystem->SetNumericAttributeBase(
			GetHealthAttribute(),
			bInstantKill ? 0.0f : FMath::Max(GetHealth() - RemainingDamage, 0.0f));
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
