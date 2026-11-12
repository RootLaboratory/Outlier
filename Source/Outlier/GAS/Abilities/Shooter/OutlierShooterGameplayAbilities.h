#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffectTypes.h"
#include "OutlierShooterGameplayAbilities.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UOutlierAbilitySystemComponent;
class UPlayerRelocationQuery;
class UOutlierArenaSubsystem;
struct FPlayerRelocationResult;

UCLASS(Abstract)
class OUTLIER_API UOutlierShooterGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterGameplayAbility();

protected:
	AShooterCharacter* GetShooterCharacter() const;
	UOutlierAbilitySystemComponent* GetOutlierAbilitySystem() const;
	bool PassesUpgradeGrantGate(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTag& AbilityTag) const;
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
	enum class ELeapPhase : uint8 { Idle, FindingInitial, Casting, FindingFinal, Applying, Finishing };
	bool HasCurrentParticipants() const;
	bool CheckLeapEligibility();
	void StartDestinationQuery(bool bExpandedOnly);
	void HandleDestinationResult(uint32 Execution, const FPlayerRelocationResult& Result);
	void BeginCast();
	void ApplyDestination();
	void CompleteQuantumLeap();

	UPROPERTY(Transient)
	TObjectPtr<UPlayerRelocationQuery> DestinationQuery;
	TWeakObjectPtr<AShooterCharacter> LeapShooter;
	TWeakObjectPtr<APartnerCharacter> LeapPartner;
	TWeakObjectPtr<UOutlierArenaSubsystem> LeapArena;
	FTransform Reference = FTransform::Identity;
	uint32 GameplayGeneration = 0;
	uint32 ExecutionSerial = 0;
	ELeapPhase Phase = ELeapPhase::Idle;
	FActiveGameplayEffectHandle QuantumLeapStateHandle;
	FActiveGameplayEffectHandle DamageImmuneHandle;
	FTimerHandle CastTimerHandle;
	bool bCommitFailureCooldown = false;
	bool bTeleportSucceeded = false;
	bool bEndRequestedDuringTeleport = false;
	bool bDeferredReplicateEnd = true;
	bool bDeferredWasCancelled = false;
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
	// On 사운드를 낸 활성화에서만 Off 사운드를 낸다.
	bool bPlayedActivationAudio = false;
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
	// On 사운드를 낸 활성화에서만 Off 사운드를 낸다.
	bool bPlayedActivationAudio = false;
	bool bCommitCooldownOnEnd = false;
};

UCLASS()
class OUTLIER_API UOutlierShooterStealthAbility : public UOutlierShooterGameplayAbility
{
	GENERATED_BODY()

public:
	UOutlierShooterStealthAbility();
	bool EndStealth(bool bCommitCooldown);
	bool TriggerAutomaticStealth();
	bool IsAutomaticStealth() const { return bAutomaticStealth; }
	void NotifyOffensiveAction();
	void NotifyIncomingDamage();

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
	bool ApplyStealthEffects(float Duration);

	TWeakObjectPtr<UOutlierAbilitySystemComponent> ShooterAbilitySystem;
	TWeakObjectPtr<UOutlierAbilitySystemComponent> PartnerAbilitySystem;
	FActiveGameplayEffectHandle ShooterStealthHandle;
	FActiveGameplayEffectHandle PartnerStealthHandle;
	FActiveGameplayEffectHandle StealthSpeedHandle;
	FDelegateHandle ShooterEffectRemovedDelegateHandle;
	FDelegateHandle PartnerEffectRemovedDelegateHandle;
	bool bEndingStealth = false;
	bool bCommitCooldownOnEnd = false;
	bool bAutomaticStealth = false;
	bool bPassiveStealth = false;
	bool bRequestAutomaticStealth = false;
};
