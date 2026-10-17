#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "OutlierWeaponAttributeSet.generated.h"

UCLASS()
class OUTLIER_API UOutlierWeaponAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UOutlierWeaponAttributeSet();

	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 기본값 1.0에 업그레이드 감소량을 합산한다. 실제 반동과 각 연출 경로에서 한 번씩만 적용한다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RecoilMultiplier, Category = "GAS|Weapon")
	FGameplayAttributeData RecoilMultiplier;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierWeaponAttributeSet, RecoilMultiplier)

protected:
	UFUNCTION()
	void OnRep_RecoilMultiplier(const FGameplayAttributeData& OldRecoilMultiplier);
};
