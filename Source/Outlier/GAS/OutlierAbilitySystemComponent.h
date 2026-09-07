#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GAS/Data/OutlierPartnerAbilityConfig.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "OutlierAbilitySystemComponent.generated.h"

class AController;
class AActor;
class APawn;

UCLASS(ClassGroup = GAS)
class OUTLIER_API UOutlierAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilitySystemComponent();

	void InitializeForActor(AActor* Actor);
	void InitializeForPawn(APawn* Pawn);
	void ClearForActor(const AActor* Actor);
	void ClearForPawn(const APawn* Pawn);
	bool ApplyDamageToSelf(
		float DamageAmount,
		AController* Instigator,
		AActor* DamageCauser,
		const FGameplayTag& DamageTag);
	bool ApplyShieldRecoveryToSelf(float Amount);
	bool ApplyShieldDeltaToSelf(float Amount);
	bool ApplyPartnerShieldDeltaToSelf(float PartnerShieldDelta, float MaxPartnerShieldDelta);
	bool ApplyDeadStateToSelf();
	bool InitializeVitalityToSelf(float MaxHealth);
	bool RestoreHealthToMax();
	FActiveGameplayEffectHandle ApplyRebootStateToSelf(float DurationSeconds);
	FActiveGameplayEffectHandle ApplyStunStateToSelf(float DurationSeconds, UObject* SourceObject);
	FActiveGameplayEffectHandle ApplyDamageImmuneStateToSelf();
	FActiveGameplayEffectHandle ApplyPossessPendingStateToSelf();
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
	// 능력 재-grant 없이 런타임에 PartnerAbilityConfig 값만 교체한다 ( 업그레이드 AbilityConfig 투영용 ).
	// ConfigurePartnerAbilities 의 grant 락과 무관하게 authority 에서 config 만 갱신한다.
	bool UpdatePartnerAbilityConfig(const FOutlierPartnerAbilityConfig& Config);
	bool TryActivatePartnerAbility(const FGameplayTag& AbilityTag);
	int32 GetGrantedPartnerAbilityCount() const;
	bool IsPartnerAbilitiesConfigured() const { return bPartnerAbilitiesConfigured; }
	// 지금 적용중인 값 ( 업그레이드 투영 결과 ).
	const FOutlierPartnerAbilityConfig& GetPartnerAbilityConfig() const { return PartnerAbilityConfig; }
	// DT 기준 base ( grant 시점에 고정. 업그레이드 재계산의 기준값이자 grant 락 비교 대상 ).
	const FOutlierPartnerAbilityConfig& GetBasePartnerAbilityConfig() const { return BasePartnerAbilityConfig; }
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
	// 능력 재-grant 없이 런타임에 SuitConfig 값만 교체한다 ( 업그레이드 AbilityConfig 투영용 ).
	// ConfigureShooterSuitAbilities 의 grant 락과 무관하게 authority 에서 config 만 갱신한다.
	bool UpdateShooterSuitConfig(const FOutlierShooterSuitConfig& Config);
	bool TryActivateShooterSuitAbility(const FGameplayTag& AbilityTag);
	bool IsShooterSuitAbilityUpgradeGrantRequired(const FGameplayTag& AbilityTag) const;
	// 테스트 전용 마스터 스위치( bNoGrantMode ). Shipping 빌드에서는 아예 안 쓰인다.
	bool IsUpgradeGrantTestModeEnabled() const { return bNoGrantMode; }
	bool IsShooterSuitConfigured() const { return bShooterSuitConfigured; }
	const FOutlierShooterSuitConfig& GetShooterSuitConfig() const { return ShooterSuitConfig; }
	const FOutlierShooterSuitConfig& GetBaseShooterSuitConfig() const { return BaseShooterSuitConfig; }
	int32 GetGrantedShooterSuitAbilityCount() const;
	bool CommitShooterQuantumLeapCooldown(float DurationMultiplier = 1.0f);
	bool IsShooterQuantumLeapCooldownActive() const;
	float GetShooterQuantumLeapCooldownRemaining() const;
	bool CancelActiveShooterQuantumLeap(bool bCommitFailureCooldown = false);
	bool CommitShooterBulletReflectionCooldown();
	bool IsShooterBulletReflectionCooldownActive() const;
	float GetShooterBulletReflectionCooldownRemaining() const;
	bool EndActiveShooterBulletReflection(bool bCommitCooldown);
	bool CommitShooterWeaponOverchargeCooldown();
	bool IsShooterWeaponOverchargeCooldownActive() const;
	float GetShooterWeaponOverchargeCooldownRemaining() const;
	bool EndActiveShooterWeaponOvercharge(bool bCommitCooldown);
	bool CommitShooterStealthCooldown();
	bool IsShooterStealthCooldownActive() const;
	float GetShooterStealthCooldownRemaining() const;
	bool EndActiveShooterStealth(bool bCommitCooldown);
	EGameplayEffectReplicationMode GetConfiguredReplicationMode() const { return ReplicationMode; }

private:
	float ResolvePartnerCooldownDuration(const FGameplayTag& CooldownTag) const;
	TSubclassOf<UGameplayEffect> ResolvePartnerCooldownEffectClass(const FGameplayTag& CooldownTag) const;

	// 현재 적용중인 값 / grant 시점에 고정된 DT base.
	// 업그레이드 투영은 앞의 것만 갱신하고, grant 락 검사는 뒤의 것과 비교한다.
	FOutlierPartnerAbilityConfig PartnerAbilityConfig;
	FOutlierPartnerAbilityConfig BasePartnerAbilityConfig;
	TArray<FGameplayAbilitySpecHandle> GrantedPartnerAbilityHandles;
	TMap<FGameplayTag, float> SuspendedPartnerCooldowns;
	bool bPartnerAbilitiesConfigured = false;
	bool bPartnerSkillCooldownsSuspended = false;
	FOutlierShooterSuitConfig ShooterSuitConfig;
	FOutlierShooterSuitConfig BaseShooterSuitConfig;
	FGameplayAbilitySpecHandle GrantedShooterQuantumLeapAbilityHandle;
	FGameplayAbilitySpecHandle GrantedShooterBulletReflectionAbilityHandle;
	FGameplayAbilitySpecHandle GrantedShooterWeaponOverchargeAbilityHandle;
	FGameplayAbilitySpecHandle GrantedShooterStealthAbilityHandle;
	bool bShooterSuitConfigured = false;

	// 이 능력이 업그레이드 트리로 실제 Grant 되어야만 발동하는지. ( 항상 이 의미로 판정됨 - Shipping/에디터 동일 )
	// true  = Ability.* 태그가 ASC 에 실제로 붙어 있어야 발동 ( GrantAbility 업그레이드 필요 ).
	// false = 태그 유무와 무관하게 항상 통과 ( 기본 지급 능력 ).
	// ( UOutlierShooterGameplayAbility::PassesUpgradeGrantGate 참고. bNoGrantMode 가 켜져 있으면
	//   에디터/개발 빌드에 한해 이 판정 자체를 통째로 건너뛴다 - 아래 주석 참고 )
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bQuantumLeapRequiresUpgradeGrant = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bBulletReflectionRequiresUpgradeGrant = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bWeaponOverchargeRequiresUpgradeGrant = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bStealthRequiresUpgradeGrant = false;

	// 테스트 전용 마스터 스위치. 기본 true.
	// - Shipping 빌드에서는 이 값을 아예 안 본다 ( 항상 위의 bXRequiresUpgradeGrant 로만 판정 ).
	// - 에디터/개발 빌드에서 true 면, 업그레이드 트리를 하나도 안 찍었어도 Shooter 슈트 능력을
	//   전부 바로 테스트할 수 있도록 Grant 판정 자체를 건너뛰고 무조건 통과시킨다.
	// - false 로 끄면 에디터/개발 빌드에서도 Shipping 과 동일한( 실제 태그 기준 ) 판정을 그대로 탄다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bNoGrantMode = true;
};
