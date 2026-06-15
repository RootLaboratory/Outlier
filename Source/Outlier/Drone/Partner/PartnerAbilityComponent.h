// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/OutlierAbilityComponent.h"
#include "PartnerAbilityComponent.generated.h"

class UPartnerHackComponent;
class UPartnerMovementComponent;
class UPartnerSupportComponent;

UCLASS(ClassGroup = (Partner), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerAbilityComponent : public UOutlierAbilityComponent
{
	GENERATED_BODY()

public:
	UPartnerAbilityComponent();

	virtual void BeginPlay() override;

protected:
	virtual void InitializeAbilityHandlers() override;

	EOutlierAbilityResult ExecuteEMP(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteShield(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteHacking(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteScan(const FOutlierAbilityRow& AbilityRow);

	void RefreshPartnerComponents();

	UPROPERTY(Transient)
	TObjectPtr<UPartnerSupportComponent> SupportComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPartnerMovementComponent> MovementComponent;
};
