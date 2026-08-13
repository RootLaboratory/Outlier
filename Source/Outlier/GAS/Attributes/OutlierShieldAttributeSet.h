#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "OutlierShieldAttributeSet.generated.h"

UCLASS()
class OUTLIER_API UOutlierShieldAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UOutlierShieldAttributeSet();

	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Shield, Category = "GAS|Shield")
	FGameplayAttributeData Shield;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierShieldAttributeSet, Shield)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxShield, Category = "GAS|Shield")
	FGameplayAttributeData MaxShield;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierShieldAttributeSet, MaxShield)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PartnerShield, Category = "GAS|Shield")
	FGameplayAttributeData PartnerShield;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierShieldAttributeSet, PartnerShield)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxPartnerShield, Category = "GAS|Shield")
	FGameplayAttributeData MaxPartnerShield;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierShieldAttributeSet, MaxPartnerShield)

protected:
	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldShield);

	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield);

	UFUNCTION()
	void OnRep_PartnerShield(const FGameplayAttributeData& OldPartnerShield);

	UFUNCTION()
	void OnRep_MaxPartnerShield(const FGameplayAttributeData& OldMaxPartnerShield);
};
