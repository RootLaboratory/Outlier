// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/OutlierAbilityComponent.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "PartnerAbilityComponent.generated.h"

class UPartnerEMPComponent;
class UPartnerMovementComponent;
class UPartnerSupportComponent;

UCLASS(ClassGroup = (Partner), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerAbilityComponent : public UOutlierAbilityComponent
{
	GENERATED_BODY()

public:
	UPartnerAbilityComponent();

	virtual void BeginPlay() override;

	void RefreshCachedPartnerAbilityData();

protected:
	virtual void InitializeAbilityHandlers() override;
	virtual EOutlierAbilityResult GetAdditionalActivationFailureReason(const FOutlierAbilityRow& AbilityRow) const override;
	virtual bool ShouldBypassCooldownForActivation(const FOutlierAbilityRow& AbilityRow) const override;
	virtual void HandleAbilityCooldownCommitted(const FOutlierAbilityRow& AbilityRow, float CooldownEndTime) override;

	EOutlierAbilityResult ExecuteEMP(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteShield(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteHacking(const FOutlierAbilityRow& AbilityRow);
	EOutlierAbilityResult ExecuteScan(const FOutlierAbilityRow& AbilityRow);

	void RefreshPartnerComponents();
	void CachePartnerAbilityData(
		const FPartnerHackAbilityData& HackAbilityData,
		float HackCooldownSeconds,
		const FPartnerEMPAbilityData& EMPAbilityData,
		float EMPCooldownSeconds);
	void NotifyAbilityCooldownUI(FGameplayTag AbilityTag, float CooldownSeconds, float CooldownEndTime) const;

	UFUNCTION(Client, Reliable)
	void ClientSyncPartnerAbilityData(
		const FPartnerHackAbilityData& HackAbilityData,
		float HackCooldownSeconds,
		const FPartnerEMPAbilityData& EMPAbilityData,
		float EMPCooldownSeconds);

	UFUNCTION(Client, Reliable)
	void ClientNotifyAbilityCooldown(FGameplayTag AbilityTag, float CooldownSeconds, float CooldownEndTime);

	UPROPERTY(Transient)
	TObjectPtr<UPartnerSupportComponent> SupportComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPartnerEMPComponent> EMPComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPartnerMovementComponent> MovementComponent;
};
