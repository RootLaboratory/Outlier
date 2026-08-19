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
