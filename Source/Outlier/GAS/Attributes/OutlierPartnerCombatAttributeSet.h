#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "OutlierPartnerCombatAttributeSet.generated.h"

UCLASS()
class OUTLIER_API UOutlierPartnerCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UOutlierPartnerCombatAttributeSet();
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BasicAttackStunMultiplier, Category = "GAS|Partner|Combat")
	FGameplayAttributeData BasicAttackStunMultiplier;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierPartnerCombatAttributeSet, BasicAttackStunMultiplier)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HackedDroneDamageMultiplier, Category = "GAS|Partner|Combat")
	FGameplayAttributeData HackedDroneDamageMultiplier;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierPartnerCombatAttributeSet, HackedDroneDamageMultiplier)
protected:
	UFUNCTION()
	void OnRep_HackedDroneDamageMultiplier(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_BasicAttackStunMultiplier(const FGameplayAttributeData& OldValue);
};
