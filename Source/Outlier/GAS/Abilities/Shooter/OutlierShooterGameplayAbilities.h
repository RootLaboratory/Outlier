#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffectTypes.h"
#include "OutlierShooterGameplayAbilities.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UOutlierAbilitySystemComponent;

UCLASS(Abstract)
class OUTLIER_API UOutlierShooterGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterGameplayAbility();

protected:
	AShooterCharacter* GetShooterCharacter() const;
	UOutlierAbilitySystemComponent* GetOutlierAbilitySystem() const;
};

UCLASS()
class OUTLIER_API UOutlierShooterQuantumLeapAbility : public UOutlierShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterQuantumLeapAbility();
	bool CancelQuantumLeap(bool bInCommitFailureCooldown);

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
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
	bool TryResolveDestination(
		const AShooterCharacter& Shooter,
		const APartnerCharacter& Partner,
		FVector& OutDestination) const;
	bool IsDestinationClear(
		const AShooterCharacter& Shooter,
		const APartnerCharacter& Partner,
		const FVector& Destination) const;
	void CompleteQuantumLeap();

	FVector Destination = FVector::ZeroVector;
	FActiveGameplayEffectHandle DamageImmuneHandle;
	FTimerHandle CastTimerHandle;
	bool bCommitFailureCooldown = false;
	bool bTeleportSucceeded = false;
};

UCLASS()
class OUTLIER_API UOutlierShooterBulletReflectionAbility : public UOutlierShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterBulletReflectionAbility();
	bool EndBulletReflection(bool bCommitCooldown);

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
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
	void HandleReflectionEffectRemoved(const FActiveGameplayEffect& RemovedEffect);

	TWeakObjectPtr<UOutlierAbilitySystemComponent> ShooterAbilitySystem;
	FActiveGameplayEffectHandle ReflectionEffectHandle;
	FDelegateHandle EffectRemovedDelegateHandle;
	bool bEndingReflection = false;
	bool bCommitCooldownOnEnd = false;
};

UCLASS()
class OUTLIER_API UOutlierShooterWeaponOverchargeAbility : public UOutlierShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterWeaponOverchargeAbility();
	bool EndWeaponOvercharge(bool bCommitCooldown);

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
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
	void DrainShield();
	void HandleOverchargeEffectRemoved(const FActiveGameplayEffect& RemovedEffect);

	TWeakObjectPtr<UOutlierAbilitySystemComponent> ShooterAbilitySystem;
	FActiveGameplayEffectHandle OverchargeEffectHandle;
	FDelegateHandle EffectRemovedDelegateHandle;
	FTimerHandle ShieldDrainTimerHandle;
	bool bEndingOvercharge = false;
	bool bCommitCooldownOnEnd = false;
};

UCLASS()
class OUTLIER_API UOutlierShooterStealthAbility : public UOutlierShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterStealthAbility();
	bool EndStealth(bool bCommitCooldown);

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
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
	void HandleShooterEffectRemoved(const FActiveGameplayEffect& RemovedEffect);
	void HandlePartnerEffectRemoved(const FActiveGameplayEffect& RemovedEffect);
	void RefreshEnemyDetection() const;

	TWeakObjectPtr<UOutlierAbilitySystemComponent> ShooterAbilitySystem;
	TWeakObjectPtr<UOutlierAbilitySystemComponent> PartnerAbilitySystem;
	FActiveGameplayEffectHandle ShooterStealthHandle;
	FActiveGameplayEffectHandle PartnerStealthHandle;
	FDelegateHandle ShooterEffectRemovedDelegateHandle;
	FDelegateHandle PartnerEffectRemovedDelegateHandle;
	bool bEndingStealth = false;
	bool bCommitCooldownOnEnd = false;
};
