#include "GAS/Attributes/OutlierPartnerMovementAttributeSet.h"

#include "Net/UnrealNetwork.h"

UOutlierPartnerMovementAttributeSet::UOutlierPartnerMovementAttributeSet()
	: MoveSpeed(300.0f)
	, BoostSpeed(500.0f)
{
}

void UOutlierPartnerMovementAttributeSet::PreAttributeBaseChange(
	const FGameplayAttribute& Attribute,
	float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);

	if (Attribute == GetMoveSpeedAttribute() || Attribute == GetBoostSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UOutlierPartnerMovementAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute,
	float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMoveSpeedAttribute() || Attribute == GetBoostSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UOutlierPartnerMovementAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(
		UOutlierPartnerMovementAttributeSet,
		MoveSpeed,
		COND_None,
		REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UOutlierPartnerMovementAttributeSet,
		BoostSpeed,
		COND_None,
		REPNOTIFY_Always);
}

void UOutlierPartnerMovementAttributeSet::OnRep_MoveSpeed(
	const FGameplayAttributeData& OldMoveSpeed)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierPartnerMovementAttributeSet, MoveSpeed, OldMoveSpeed);
}

void UOutlierPartnerMovementAttributeSet::OnRep_BoostSpeed(
	const FGameplayAttributeData& OldBoostSpeed)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UOutlierPartnerMovementAttributeSet, BoostSpeed, OldBoostSpeed);
}
