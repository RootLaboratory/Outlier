#include "GAS/Attributes/OutlierVitalAttributeSet.h"

#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
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
	LastShieldDamage = 0.0f;
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
	if (OutlierDamage::IsEditorInvulnerable(AbilitySystem->GetAvatarActor())
		|| OutlierDamage::IsDeveloperInvincible(AbilitySystem->GetAvatarActor()))
	{
		return;
	}

	float RemainingDamage = Damage;
	bool bDamageApplied = false;
	const bool bInstantKill = Data.EffectSpec.GetDynamicAssetTags().HasTagExact(OutlierDamage::InstantKillTag());
	if (const UOutlierShieldAttributeSet* ShieldSet = AbilitySystem->GetSet<UOutlierShieldAttributeSet>())
	{
		// 즉사도 일반 피해와 같은 실드 → 체력 순서로 처리한다. 큰 수의 합산/반올림에 의존하지 않는다.
		const float PartnerShieldBefore = ShieldSet->GetPartnerShield();
		const float PartnerAbsorption = bInstantKill ? PartnerShieldBefore : FMath::Min(PartnerShieldBefore, RemainingDamage);
		if (PartnerAbsorption > 0.0f)
		{
			const float PartnerShieldAfter = PartnerShieldBefore - PartnerAbsorption;
			bDamageApplied |= PartnerShieldAfter < PartnerShieldBefore;
			LastShieldDamage += PartnerAbsorption;
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetPartnerShieldAttribute(),
				PartnerShieldAfter);
			RemainingDamage -= PartnerAbsorption;
		}

		const float ShieldBefore = ShieldSet->GetShield();
		const float ShieldAbsorption = bInstantKill ? ShieldBefore : FMath::Min(ShieldBefore, RemainingDamage);
		if (ShieldAbsorption > 0.0f)
		{
			const float ShieldAfter = ShieldBefore - ShieldAbsorption;
			bDamageApplied |= ShieldAfter < ShieldBefore;
			LastShieldDamage += ShieldAbsorption;
			AbilitySystem->SetNumericAttributeBase(
				UOutlierShieldAttributeSet::GetShieldAttribute(),
				ShieldAfter);
			RemainingDamage -= ShieldAbsorption;
		}
	}

	if (bInstantKill || RemainingDamage > 0.0f)
	{
		const float HealthBefore = GetHealth();
		const float HealthAfter = bInstantKill ? 0.0f : FMath::Max(HealthBefore - RemainingDamage, 0.0f);
		bDamageApplied |= HealthAfter < HealthBefore;
		// UI 피드백 단계는 실드를 넘어 Health 에 도달한 양으로 정한다. 즉사는 남은 Health 전체를 기록한다.
		LastHealthDamage = bInstantKill ? FMath::Max(HealthBefore, 0.0f) : RemainingDamage;
		AbilitySystem->SetNumericAttributeBase(GetHealthAttribute(), HealthAfter);
	}

	if (bDamageApplied && AbilitySystem->IsOwnerActorAuthoritative())
	{
		if (UOutlierAbilitySystemComponent* OutlierASC = Cast<UOutlierAbilitySystemComponent>(AbilitySystem))
		{
			OutlierASC->OnDamageApplied.Broadcast();
		}
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
