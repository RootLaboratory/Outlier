#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "OutlierVitalAttributeSet.generated.h"

#define OUTLIER_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class OUTLIER_API UOutlierVitalAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UOutlierVitalAttributeSet();

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "GAS|Vital")
	FGameplayAttributeData Health;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierVitalAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "GAS|Vital")
	FGameplayAttributeData MaxHealth;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierVitalAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "GAS|Vital")
	FGameplayAttributeData IncomingDamage;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierVitalAttributeSet, IncomingDamage)

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
};
