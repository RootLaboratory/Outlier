#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GAS/Data/OutlierPartnerAbilityConfig.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "OutlierAbilitySystemComponent.generated.h"

class AController;
class AActor;
class APawn;
struct FOutlierStealthSession;

// 서버의 한 충전 주기를 통째로 전달한다. UI가 남은 시간을 새 쿨타임으로 오해하지 않게 한다.
USTRUCT()
struct FOutlierQuantumLeapChargeState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 CurrentCharges = 1;
	UPROPERTY()
	int32 MaxCharges = 1;
	UPROPERTY()
	float RechargeStartServerTime = 0.0f;
	UPROPERTY()
	float RechargeDuration = 0.0f;
	// 자연 회복만 증가시킨다. 횟수 증가만으로 업그레이드 보상과 회복을 혼동하지 않게 한다.
	UPROPERTY()
	uint32 RecoverySerial = 0;
	UPROPERTY()
	bool bInitialized = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOutlierQuantumLeapChargesChanged, bool /* bRecovered */);

UCLASS(ClassGroup = GAS)
class OUTLIER_API UOutlierAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilitySystemComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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
	// Grant 판정 규칙( 기본 제공이거나 Grant 근거가 있으면 통과 ). 근거 태그의 출처는 호출부가 고른다.
	// 발동 게이트는 ASC 소유 태그를, UI 는 업그레이드 캐시( 클라에서 GE 복제를 기다리지 않음 )를 넘긴다.
	bool IsShooterSuitAbilityGranted(const FGameplayTag& AbilityTag, bool bHasGrantTag) const;
#if WITH_EDITOR
	// PIE 테스트 도구에서만 변경한다. 서버/소유 클라이언트가 같은 해금 규칙을 사용한다.
	void SetEditorShooterSuitGrantChecks(bool bEnabled);
	bool AreEditorShooterSuitGrantChecksEnabled() const { return bEditorShooterSuitGrantChecks; }
#endif
	// 기본 제공( Requires...UpgradeGrant == false ) 능력. 기본 선택과 잠김 폴백이 쓴다. 없으면 invalid.
	FGameplayTag GetDefaultShooterSuitAbilityTag() const;
	bool IsShooterSuitConfigured() const { return bShooterSuitConfigured; }
	const FOutlierShooterSuitConfig& GetShooterSuitConfig() const { return ShooterSuitConfig; }
	const FOutlierShooterSuitConfig& GetBaseShooterSuitConfig() const { return BaseShooterSuitConfig; }
	int32 GetGrantedShooterSuitAbilityCount() const;
	// 기존 호출 이름을 유지한다. 성공한 도약의 충전 1회 소모와 순차 재충전 시작을 함께 처리한다.
	bool CommitShooterQuantumLeapCooldown();
	void RefreshShooterQuantumLeapCharges();
	const FOutlierQuantumLeapChargeState& GetShooterQuantumLeapChargeState() const { return QuantumLeapCharges; }
	FOutlierQuantumLeapChargesChanged OnQuantumLeapChargesChanged;
	bool CommitShooterQuantumLeapRetryDelay();
	bool IsShooterQuantumLeapRetryActive() const;
	void RefreshShooterQuantumLeapImmunity();
	void ResetShooterQuantumLeapTransientState();
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
	TSharedPtr<FOutlierStealthSession> BeginShooterStealthAttack();
	void BeginShooterStealthSession();
	void FinishShooterStealthSession();
	void NotifyShooterStealthDamage();
	void NotifyShooterStealthKill(const TSharedPtr<FOutlierStealthSession>& Session, bool bDrone);
	void ResetShooterStealthUpgradeState();
	void ApplyShooterStealthExitSpeedBoost();
	EGameplayEffectReplicationMode GetConfiguredReplicationMode() const { return ReplicationMode; }

private:
	UFUNCTION()
	void OnRep_EditorShooterSuitGrantChecks();

	// Editor 빌드의 PIE에서만 복제/설정한다. 실제 업그레이드 노드가 해금의 근거다.
	UPROPERTY(Transient, ReplicatedUsing = OnRep_EditorShooterSuitGrantChecks)
	bool bEditorShooterSuitGrantChecks = false;

	UFUNCTION()
	void OnRep_QuantumLeapCharges(const FOutlierQuantumLeapChargeState& PreviousState);
	void HandleQuantumLeapChargeUpgrade(const FGameplayTag Tag, int32 NewCount);
	void BeginQuantumLeapRecharge();
	void HandleQuantumLeapRechargeRemoved(const FGameplayEffectRemovalInfo& RemovalInfo, FActiveGameplayEffectHandle ExpectedHandle);
	void NotifyQuantumLeapChargesChanged(bool bRecovered);
	void StopQuantumLeapRecharge();
	FDelegateHandle QuantumLeapChargeUpgradeHandle;
	FActiveGameplayEffectHandle QuantumLeapRechargeHandle;
	UPROPERTY(ReplicatedUsing = OnRep_QuantumLeapCharges)
	FOutlierQuantumLeapChargeState QuantumLeapCharges;

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
	TSharedPtr<FOutlierStealthSession> ShooterStealthSession;
	int32 StealthDroneKillCount = 0;
	uint32 StealthGeneration = 0;
	float PendingStealthCooldownReduction = 0.0f;
	FActiveGameplayEffectHandle StealthExitSpeedHandle;
	// 능력 종료 후에도 유지되는 도약 무적과 실패 제한만 추적한다. 시전 중 무적은 능력이 관리한다.
	FActiveGameplayEffectHandle QuantumLeapImmunityHandle;
	FActiveGameplayEffectHandle QuantumLeapRetryHandle;

	// 이 능력이 업그레이드 트리로 실제 Grant 되어야만 발동하는지. ( 항상 이 의미로 판정됨 - Shipping/에디터 동일 )
	// true  = Ability.* 태그가 ASC 에 실제로 붙어 있어야 발동 ( GrantAbility 업그레이드 필요 ).
	// false = 태그 유무와 무관하게 항상 통과 ( 기본 지급 능력 ).
	// ( IsShooterSuitAbilityGranted 참고. bNoGrantMode 가 켜져 있으면
	//   에디터/개발 빌드에 한해 이 판정 자체를 통째로 건너뛴다 - 아래 주석 참고 )
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bQuantumLeapRequiresUpgradeGrant = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bBulletReflectionRequiresUpgradeGrant = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bWeaponOverchargeRequiresUpgradeGrant = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bStealthRequiresUpgradeGrant = true;

	// 테스트 전용 마스터 스위치. 기본 true.
	// - Shipping 빌드에서는 이 값을 아예 안 본다 ( 항상 위의 bXRequiresUpgradeGrant 로만 판정 ).
	// - 에디터/개발 빌드에서 true 면, 업그레이드 트리를 하나도 안 찍었어도 Shooter 슈트 능력을
	//   전부 바로 테스트할 수 있도록 Grant 판정 자체를 건너뛰고 무조건 통과시킨다.
	// - false 로 끄면 에디터/개발 빌드에서도 Shipping 과 동일한( 실제 태그 기준 ) 판정을 그대로 탄다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Shooter Suit|Upgrade Grant", meta = (AllowPrivateAccess = "true"))
	bool bNoGrantMode = true;
};
