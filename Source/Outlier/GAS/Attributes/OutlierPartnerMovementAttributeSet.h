#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "OutlierPartnerMovementAttributeSet.generated.h"

UCLASS()
class OUTLIER_API UOutlierPartnerMovementAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UOutlierPartnerMovementAttributeSet();

	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeed, Category = "GAS|Partner|Movement")
	FGameplayAttributeData MoveSpeed;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierPartnerMovementAttributeSet, MoveSpeed)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BoostSpeed, Category = "GAS|Partner|Movement")
	FGameplayAttributeData BoostSpeed;
	OUTLIER_ATTRIBUTE_ACCESSORS(UOutlierPartnerMovementAttributeSet, BoostSpeed)

protected:
	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed);

	UFUNCTION()
	void OnRep_BoostSpeed(const FGameplayAttributeData& OldBoostSpeed);
};
