#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Drone/Partner/HackType.h"
#include "OutlierPartnerGameplayAbilities.generated.h"

class APartnerCharacter;
class UOutlierAbilitySystemComponent;
class UPartnerEMPComponent;
class UPartnerHackComponent;
class UPartnerSupportComponent;

UCLASS(Abstract)
class OUTLIER_API UOutlierPartnerGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierPartnerGameplayAbility();
	bool BlocksPartnerAbilityExecution() const;

protected:
	void ConfigurePartnerAbilityTags(const FGameplayTag& AbilityTag, const FGameplayTag& CooldownTag);
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	APartnerCharacter* GetPartnerCharacter() const;
	UOutlierAbilitySystemComponent* GetOutlierAbilitySystem() const;
	bool CommitConfiguredCooldown(float OverrideDuration = 0.0f) const;

	FGameplayTag CooldownTag;
};

UCLASS()
class OUTLIER_API UOutlierPartnerEMPAbility : public UOutlierPartnerGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierPartnerEMPAbility();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void HandleEMPFinished(bool bAppliedTargets, bool bCancelled);
	TWeakObjectPtr<UPartnerEMPComponent> ActiveEMPComponent;
	FDelegateHandle EMPFinishedHandle;
};

UCLASS()
class OUTLIER_API UOutlierPartnerShieldAbility : public UOutlierPartnerGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierPartnerShieldAbility();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class OUTLIER_API UOutlierPartnerHackAbility : public UOutlierPartnerGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierPartnerHackAbility();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void HandleHackFinished(EHackResult Result, bool bPossessionTarget);
	TWeakObjectPtr<UPartnerHackComponent> ActiveHackComponent;
	FDelegateHandle HackFinishedHandle;
};

UCLASS()
class OUTLIER_API UOutlierPartnerScanAbility : public UOutlierPartnerGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierPartnerScanAbility();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void HandleScanFinished();
	TWeakObjectPtr<UPartnerSupportComponent> ActiveSupportComponent;
	FDelegateHandle ScanFinishedHandle;
};
