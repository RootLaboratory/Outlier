#include "GAS/Attributes/OutlierWeaponAttributeSet.h"

#include "Net/UnrealNetwork.h"

UOutlierWeaponAttributeSet::UOutlierWeaponAttributeSet()
	: RecoilMultiplier(1.0f)
{
}

void UOutlierWeaponAttributeSet::PreAttributeBaseChange(
	const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (Attribute == GetRecoilMultiplierAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UOutlierWeaponAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetRecoilMultiplierAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UOutlierWeaponAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 소유자의 1인칭뿐 아니라 다른 클라이언트의 3인칭 반동도 같은 배율을 사용한다.
	DOREPLIFETIME_CONDITION_NOTIFY(
		UOutlierWeaponAttributeSet, RecoilMultiplier, COND_None, REPNOTIFY_Always);
}

void UOutlierWeaponAttributeSet::OnRep_RecoilMultiplier(
	const FGameplayAttributeData& OldRecoilMultiplier)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierWeaponAttributeSet, RecoilMultiplier, OldRecoilMultiplier);
}
