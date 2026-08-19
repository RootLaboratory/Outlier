#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "OutlierAbilitySystemComponent.generated.h"

class APawn;
class AController;

USTRUCT()
struct OUTLIER_API FOutlierPartnerAbilityConfig
{
	GENERATED_BODY()

	float EMPCooldown = 0.0f;
	float ShieldCooldown = 0.0f;
	float HackCooldown = 0.0f;
	float ScanCooldown = 0.0f;
	float ScanDuration = 0.0f;

	bool IsValid(FString& OutError) const;
	bool Equals(const FOutlierPartnerAbilityConfig& Other) const;
};

UCLASS(ClassGroup = GAS)
class OUTLIER_API UOutlierAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilitySystemComponent();

	void InitializeForPawn(APawn* Pawn);
	void ClearForPawn(const APawn* Pawn);
	bool ApplyDamageToSelf(
		float DamageAmount,
		AController* Instigator,
		AActor* DamageCauser,
		const FGameplayTag& DamageTag);
	bool ApplyShieldRecoveryToSelf(float Amount);
	bool ApplyPartnerShieldDeltaToSelf(float PartnerShieldDelta, float MaxPartnerShieldDelta);
	bool ApplyDeadStateToSelf();
	bool InitializeVitalityToSelf(float MaxHealth);
	bool RestoreHealthToMax();
	FActiveGameplayEffectHandle ApplyRebootStateToSelf(float DurationSeconds);
	FActiveGameplayEffectHandle ApplyDamageImmuneStateToSelf();
	FActiveGameplayEffectHandle ApplyTimedGameplayEffectToSelf(
		TSubclassOf<UGameplayEffect> EffectClass,
		float DurationSeconds,
		UObject* SourceObject);
	bool RemoveActiveEffectFromSelf(FActiveGameplayEffectHandle Handle);
	FActiveGameplayEffectHandle CommitTimedCooldown(
		TSubclassOf<UGameplayEffect> EffectClass,
		const FGameplayTag& CooldownTag,
		float DurationSeconds,
		UObject* SourceObject);
	bool IsTimedCooldownActive(const FGameplayTag& CooldownTag, const UObject* SourceObject) const;
	float GetTimedCooldownRemaining(const FGameplayTag& CooldownTag, const UObject* SourceObject) const;
	bool ConfigurePartnerAbilities(const FOutlierPartnerAbilityConfig& Config);
	bool TryActivatePartnerAbility(const FGameplayTag& AbilityTag);
	int32 GetGrantedPartnerAbilityCount() const;
	const FOutlierPartnerAbilityConfig& GetPartnerAbilityConfig() const { return PartnerAbilityConfig; }
	bool CommitPartnerCooldown(const FGameplayTag& CooldownTag, float OverrideDuration = 0.0f);
	bool IsPartnerCooldownActive(const FGameplayTag& CooldownTag) const;
	float GetPartnerCooldownRemaining(const FGameplayTag& CooldownTag) const;
	bool SuspendPartnerSkillCooldownsForPossession();
	bool ResumePartnerSkillCooldownsAfterPossession();
	void DiscardSuspendedPartnerSkillCooldowns();
	float GetSuspendedPartnerCooldownRemaining(const FGameplayTag& CooldownTag) const;
	bool ArePartnerSkillCooldownsSuspended() const { return bPartnerSkillCooldownsSuspended; }
	void CancelActivePartnerAbilities();
	bool ConfigureShooterSuitAbilities(const FOutlierShooterSuitConfig& Config);
	bool TryActivateShooterSuitAbility(const FGameplayTag& AbilityTag);
	bool IsShooterSuitConfigured() const { return bShooterSuitConfigured; }
	const FOutlierShooterSuitConfig& GetShooterSuitConfig() const { return ShooterSuitConfig; }
	int32 GetGrantedShooterSuitAbilityCount() const;
	bool CommitShooterQuantumLeapCooldown(float DurationMultiplier = 1.0f);
	bool IsShooterQuantumLeapCooldownActive() const;
	float GetShooterQuantumLeapCooldownRemaining() const;
	bool CancelActiveShooterQuantumLeap(bool bCommitFailureCooldown = false);
	bool CommitShooterBulletReflectionCooldown();
	bool IsShooterBulletReflectionCooldownActive() const;
	float GetShooterBulletReflectionCooldownRemaining() const;
	bool EndActiveShooterBulletReflection(bool bCommitCooldown);
	bool CommitShooterStealthCooldown();
	bool IsShooterStealthCooldownActive() const;
	float GetShooterStealthCooldownRemaining() const;
	bool EndActiveShooterStealth(bool bCommitCooldown);
	EGameplayEffectReplicationMode GetConfiguredReplicationMode() const { return ReplicationMode; }

private:
	float ResolvePartnerCooldownDuration(const FGameplayTag& CooldownTag) const;
	TSubclassOf<UGameplayEffect> ResolvePartnerCooldownEffectClass(const FGameplayTag& CooldownTag) const;

	FOutlierPartnerAbilityConfig PartnerAbilityConfig;
	TArray<FGameplayAbilitySpecHandle> GrantedPartnerAbilityHandles;
	TMap<FGameplayTag, float> SuspendedPartnerCooldowns;
	bool bPartnerAbilitiesConfigured = false;
	bool bPartnerSkillCooldownsSuspended = false;
	FOutlierShooterSuitConfig ShooterSuitConfig;
	FGameplayAbilitySpecHandle GrantedShooterQuantumLeapAbilityHandle;
	FGameplayAbilitySpecHandle GrantedShooterBulletReflectionAbilityHandle;
	FGameplayAbilitySpecHandle GrantedShooterStealthAbilityHandle;
	bool bShooterSuitConfigured = false;
};
