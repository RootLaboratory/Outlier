// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemInterface.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "ShooterCharacter.generated.h"

class UInputAction;
struct FInputActionValue;
class AWeaponBase;
class UShooterHealthComponent;
class UShooterInventoryComponent;
class UShooterCombatComponent;
class UShooterMovementComponent;
class ULocalPlayerUISubSystem;
enum class EWeaponType : uint8;
class UAnimMontage;
class UCurveFloat;
class APartnerCharacter;
class UOutlierAbilitySystemComponent;
class UOutlierVitalAttributeSet;
class UOutlierShieldAttributeSet;
class UDataTable;
struct FOnAttributeChangeData;
struct FOutlierTaggedDamageEvent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShooterHealthChanged, float /*CurrentHealth*/, float /*MaxHealth*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShooterShieldChanged, float /*CurrentShield*/, float /*MaxShield*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShooterPartnerShieldChanged, float /*CurrentPartnerShield*/, float /*MaxPartnerShield*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnShooterConditionChanged, const FGameplayTag& /*ConditionTag*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnShooterDynamicCrosshairChanged, bool /*bAiming*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShooterAim, bool /*bAiming*/, int32 /*WeaponStencilValue*/);

UENUM(BlueprintType)
enum class EMovementState : uint8
{
	Idle,
	Walk,
	Run,
	Crouch,
	Jump,
	Slide
};

UENUM(BlueprintType)
enum class EWeaponMode : uint8
{
	None,
	Primary,	// 주무기
	Secondary,  // 보조무기
	Melee		// 근접무기
};

UENUM(BlueprintType)
enum class ECombatState : uint8
{
	Idle,
	Fire,
	Aim,
	Reload,
	Cooldown,	// 보조무기용
	Attack		// 근접무기용
};

UENUM(BlueprintType)
enum class EShooterActionLock : uint8
{
	None,
	Equip,
	Reload,
	Slide
};

UENUM(BlueprintType)
enum class ESlideEndReason : uint8
{
	Finished,       // 정상 종료
	JumpCancel,     // 점프 입력으로 끊김
	WallCancel,     // 벽 충돌로 끊김
	FallCancel,     // 지면 이탈로 끊김
	ForcedCancel    // 사망, 강제 상태 변경 등
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCharacterDeath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMovementStateChanged, EMovementState, NewState);

UENUM()
enum class EShooterMontageAction : uint8
{
	Fire,
	Reload,
	Slide,
	Equip
};

/**
 * 
 */
UCLASS(abstract)
class OUTLIER_API AShooterCharacter : public AFirstPersonCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

	friend class UShooterHealthComponent;
	friend class UShooterInventoryComponent;
	friend class UShooterCombatComponent;
	friend class UShooterMovementComponent;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UOutlierAbilitySystemComponent> OutlierAbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UOutlierVitalAttributeSet> VitalAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UOutlierShieldAttributeSet> ShieldAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterInventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterMovementComponent> MovementComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	float MaxHP = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float WalkSpeed = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float LeanInterpSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float MaxLeanAngle = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float AimCameraFOV = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float SprintCameraFOV = 95.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraFOVInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float AimCameraFOVInterpInSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float AimCameraFOVInterpOutSpeed = 14.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Sensitivity", meta = (ClampMin = "0.0"))
	float AimLookSensitivityScale = 0.65f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Sensitivity", meta = (ClampMin = "0.0"))
	float ReloadLookSensitivityScale = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Sensitivity", meta = (ClampMin = "0.0"))
	float SprintLookSensitivityScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideDuration = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float MinSlideSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideWallStopDotThreshold = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideSpeedMultiplier = 1.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	TObjectPtr<UCurveFloat> SlideSpeedCurve;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ShadowMesh;

	/// Animation Assets
	// Fire
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> FirstPersonFireMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonFireMontage;

	// Slide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> FirstPersonSlideMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonSlideMontage;

	// Reload
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> FirstPersonReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonReloadMontage;

	// Equip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> FirstPersonEquipMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonEquipMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Sections")
	FName RifleMontageSectionName = TEXT("Rifle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Sections")
	FName PistolMontageSectionName = TEXT("Pistol");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Sections")
	FName DefaultMontageSectionName = TEXT("Default");

	// Replicated Gameplay State
	UPROPERTY(ReplicatedUsing = OnRep_MovementState, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EMovementState MovementState = EMovementState::Idle;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EWeaponMode WeaponMode = EWeaponMode::None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	ECombatState CombatState = ECombatState::Idle;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EShooterActionLock ActionLock = EShooterActionLock::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Suit")
	TObjectPtr<UDataTable> ShooterSuitAbilityDataTable;

	// Local Runtime State
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 bIsSuitMenuOpen : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State")
	uint8 bIsEquipping : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	FGameplayTag SelectedAbilityTag;

	// Lean Runtime Data
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	float CurrentLeanAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	float TargetLeanAlpha = 0.0f;

	FVector  BaseFirstPersonMeshLocation = FVector::ZeroVector;
	FVector  BaseFirstPersonViewModelRootLocation = FVector::ZeroVector;
	FRotator BaseFirstPersonCameraRootRotation = FRotator::ZeroRotator;
	FRotator BaseFirstPersonViewModelRootRotation = FRotator::ZeroRotator;
	FRotator BaseFirstPersonMeshRotation = FRotator::ZeroRotator;
	float BaseCameraFOV = 90.0f;
	// Timers
	FTimerHandle LeanUpdateTimerHandle;

	FTimerHandle ActionLockTimerHandle;

	FTimerHandle PartnerShieldTimerHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield")
	float MaxShield = 100.0f;

	float PartnerShieldDuration = 0.0f;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;
	FDelegateHandle ShieldChangedHandle;
	FDelegateHandle MaxShieldChangedHandle;
	FDelegateHandle PartnerShieldChangedHandle;
	FDelegateHandle MaxPartnerShieldChangedHandle;
	FDelegateHandle DeadTagChangedHandle;
	FDelegateHandle StealthTagChangedHandle;
	FDelegateHandle BulletReflectionTagChangedHandle;
	FDelegateHandle WeaponOverchargeTagChangedHandle;
	FDelegateHandle QuantumLeapCooldownTagChangedHandle;
	FDelegateHandle BulletReflectionCooldownTagChangedHandle;
	FDelegateHandle WeaponOverchargeCooldownTagChangedHandle;
	FDelegateHandle StealthCooldownTagChangedHandle;
	FDelegateHandle PartnerRebootTagChangedHandle;

	UPROPERTY()
	TObjectPtr<APartnerCharacter> CachedPartnerCharacter;

	bool bSuitDisabledByPartnerBoundary = false;
	bool bShooterSuitDataInitialized = false;
	FOutlierShooterSuitConfig ShooterSuitConfig;

	// Slide
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	float SlideCameraEffectInterpInSpeed = 18.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	float SlideCameraEffectInterpOutSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Slide")
	TObjectPtr<UCurveFloat> SlideCameraRollCurve = nullptr;

	float TargetSlideCameraEffectAlpha = 0.0f;
	float CurrentSlideCameraEffectAlpha = 0.0f;

	float SlideCameraEffectElapsedTime = 0.0f;
	float SlideCameraEffectDuration = 0.0f;

	float TargetSlideCameraRollDegrees = 0.0f;

	float ActiveSlideCameraRollDegrees = 0.0f;

protected:
	// Engine Lifecycle
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	void RefreshAbilitySystemActorInfo();
	void InitializeGasVitality();
	void InitializeGasSuitAbilities();
	void BindGasVitalityObservers();
	void UnbindGasVitalityObservers();
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleShieldChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxShieldChanged(const FOnAttributeChangeData& ChangeData);
	void HandlePartnerShieldChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxPartnerShieldChanged(const FOnAttributeChangeData& ChangeData);
	void HandleDeadTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleStealthTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleBulletReflectionTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleWeaponOverchargeTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleQuantumLeapCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleBulletReflectionCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleWeaponOverchargeCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleStealthCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void RefreshShooterSuitCooldownUI();
	void BindPartnerSuitStateObserver();
	void UnbindPartnerSuitStateObserver();
	void HandlePartnerRebootTagChanged(const FGameplayTag Tag, int32 NewCount);
	void RefreshShooterSuitAvailabilityUI();

	UFUNCTION(BlueprintImplementableEvent, Category = "Suit|Bullet Reflection")
	void BP_OnBulletReflectionStateChanged(bool bActive);

	UFUNCTION(BlueprintImplementableEvent, Category = "Suit|Weapon Overcharge")
	void BP_OnWeaponOverchargeStateChanged(bool bActive);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastNotifyBulletReflected(FVector ReflectionStart, FVector ReflectionEnd);

	UFUNCTION(BlueprintImplementableEvent, Category = "Suit|Bullet Reflection")
	void BP_OnBulletReflected(FVector ReflectionStart, FVector ReflectionEnd);

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	virtual void Landed(const FHitResult& Hit) override;

	virtual void OnMovementModeChanged(EMovementMode  PrevMovementMode, uint8 PreviousCustomMode) override;

	virtual void OnMoveInputUpdated(const FVector2D& MoveValue);

	virtual void LookInput(const FInputActionValue& Value) override;
public:
	// Construction
	/** Constructor */
	AShooterCharacter();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UOutlierAbilitySystemComponent* GetOutlierAbilitySystemComponent() const
	{
		return OutlierAbilitySystemComponent;
	}
	const UOutlierVitalAttributeSet* GetVitalAttributeSet() const { return VitalAttributeSet; }
	const UOutlierShieldAttributeSet* GetShieldAttributeSet() const { return ShieldAttributeSet; }

	// Events
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnCharacterDeath OnCharacterDeath;

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnMovementStateChanged OnMovementStateChanged;

	FOnShooterHealthChanged OnShooterHealthChanged;
	FOnShooterShieldChanged OnShooterShieldChanged;
	FOnShooterPartnerShieldChanged OnShooterPartnerShieldChanged;
	FOnShooterConditionChanged OnShooterConditionChanged;
	FOnShooterDynamicCrosshairChanged OnShooterDynamicCrosshairChanged;
	FOnShooterAim OnShooterAimingBlur;
	// Weapon Socket Queries
	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;
	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;
	USkeletalMeshComponent* GetShadowMesh() const { return ShadowMesh; }

	// Replication / Engine Hooks
	UFUNCTION()
	void OnRep_MovementState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Unreal 공통 피해 진입점을 기존 Shooter 실드 및 HP 처리로 연결한다.
	virtual float TakeDamage(
		float DamageAmount,
		FDamageEvent const& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser) override;
	virtual void EquipWeapon(AWeaponBase* Weapon) override;
	virtual FGameplayTagContainer GetOwnedGameplayTagsForQuery() const override;

	// Read-only Queries
	float GetAimYawForAnimation() const;
	float GetAimPitchForAnimation() const;

	bool CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const;
	bool CanAimInCurrentState() const;
	bool CanReloadInCurrentState() const;
	bool CanFireInCurrentState() const;
	virtual bool CanInteract() const override;
	bool CanLean() const;

	bool WantsToAim() const;
	bool IsAiming() const;
	bool IsSliding() const;
	bool IsSprinting() const;
	bool IsSlidingCanceled() const;

	void SetPartnerCharacter(APartnerCharacter* NewPartner);
	void SetSuitDisabledByPartnerBoundary(bool bDisabled);
	APartnerCharacter* GetPartnerCharacter() const { return CachedPartnerCharacter; }
	bool IsSuitDisabledByPartnerBoundary() const { return bSuitDisabledByPartnerBoundary; }
	bool IsShooterSuitUseDisabled() const;
	bool IsBulletReflecting() const;
	bool IsWeaponOvercharged() const;
	bool BeginWeaponOvercharge();
	void FinishWeaponOvercharge(float ShieldRecoveryDelay);
	void NotifyOffensiveActionExecuted();
	UFUNCTION(BlueprintCallable, Category = "Suit|Stealth")
	void NotifyStealthDetected();
	bool CancelActiveQuantumLeap(bool bCommitFailureCooldown = false);
	bool EndActiveBulletReflection(bool bCommitCooldown);
	bool EndActiveWeaponOvercharge(bool bCommitCooldown);
	bool EndActiveStealth(bool bCommitCooldown);

	void ApplyPartnerShield(float Amount, float Duration);
	float GetCurPartnerShield() const;
	float GetMaxPartnerShield() const;
	float GetCurShield() const;
	float GetMaxShield() const;
	float GetCurHealth() const;
	float GetMaxHealth() const;
	void BroadcastCurrentUIState();

	UFUNCTION(BlueprintPure)
	float GetCurrentLeanAlpha() const { return CurrentLeanAlpha; }

	UFUNCTION(BlueprintPure)
	float GetCurrentLeanRollDegrees() const { return CurrentLeanAlpha * MaxLeanAngle; }

	UFUNCTION(BlueprintPure)
	float GetCurrentSlideCameraRollDegrees() const { return ActiveSlideCameraRollDegrees; }

	UFUNCTION(BlueprintPure)
	float GetMaxLeanAngle() const { return MaxLeanAngle; }

	UFUNCTION(BlueprintPure)
	EMovementState GetMovementState() const { return MovementState; }

	UFUNCTION(BlueprintPure)
	ECombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure)
	EWeaponMode GetWeaponMode() const { return WeaponMode; }

	UFUNCTION(BlueprintPure)
	EShooterActionLock GetActionLock() const { return ActionLock; }

	UFUNCTION(BlueprintPure)
	bool IsActionLocked() const { return ActionLock != EShooterActionLock::None; }

	UShooterInventoryComponent* GetInventoryComponent() { return InventoryComponent; }

	UFUNCTION(BlueprintPure)
	bool IsReloading() const;

	UFUNCTION(BlueprintPure)
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "Suit|Stealth")
	bool IsStealthed() const;

	void HandleWeaponAttackStoppedInternal();
	void HandleAutoReloadRequested();
	void HandleFireShotAnimation();
	// Blueprint / Notify Entry Points
	UFUNCTION(BlueprintCallable, Category = "Animation|Notify")
	void HandleReloadCommitNotify();

	void DoJumpStart();

	void DoJumpEnd();
protected:
	bool TryReflectIncomingDamage(
		float DamageAmount,
		const FOutlierTaggedDamageEvent& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser);
	void ApplyDamageInternal(
		float DamageAmount,
		AController* EventInstigator,
		AActor* DamageCauser,
		const FGameplayTag& DamageTag);
	void UpdatePartnerShieldDecay();

	// Input Handlers
	virtual void TryStartAttack() override;
	virtual void TryStopAttack() override;

	void TryReload();
	void TrySwitchWeapon1();
	void TrySwitchWeapon2();
	void TrySwitchWeapon3();
	void SelectWeaponByIndex(int32 SlotIndex);

	void HandleAimPressed();
	void HandleAimReleased();

	void HandleSprintPressed();
	void HandleSprintReleased();

	void HandleCrouchToggled();

	void TryOpenSuitMenu();
	void TryHandleSuitMenuHover();
	void TryCloseSuitMenu();
	void UpdateSuitSelection(const FInputActionValue& Value);
	void TryUseSuit();
	void SetStealthVisualEnabled(bool bEnabled);
	void TrySlide();
	void TryLean(const FInputActionValue& Value);
	void StopLean();

	void RefreshFirstPersonShadowPolicy();
	void UpdateSlideCameraEffect(float DeltaSeconds);

	// Server RPC
	UFUNCTION(Server, Reliable)
	void ServerStartAttack();

	UFUNCTION(Server, Reliable)
	void ServerStopAttack();

	UFUNCTION(Server, Reliable)
	void ServerReload();

	UFUNCTION(Server, Reliable)
	void ServerSelectWeaponByIndex(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerSetAimState(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerSetSprintState(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void ServerRequestCrouchOrSlide();

	UFUNCTION(Server, Reliable)
	void ServerRequestUncrouch();

	UFUNCTION(Server, Reliable)
	void ServerJumpStart();

	UFUNCTION(Server, Reliable)
	void ServerJumpEnd();

	UFUNCTION(Server, Reliable)
	void ServerUseSuitAbility(FGameplayTag AbilityTag);

	UFUNCTION(Client, Reliable)
	void ClientPlayFirstPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayThirdPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType);


public:
	void RefreshMovementState();
	void RefreshCombatState();
	void RefreshWeaponMode();
	void ResolveStateConflicts();
	void SetMovementStateImmediate(EMovementState NewState);

	// Internal Helpers
	void StopSprintInternal();
	void StopAimInternal();
	void BeginReloadInternal();
	void CancelReloadInternal();
	void FinishReloadInternal();
	bool CanStartAction(EShooterActionLock NextLock) const;
	void BeginActionLock(EShooterActionLock NewLock);
	void EndActionLock(EShooterActionLock LockToEnd);

	void StartLeanUpdate();
	void StopLeanUpdateIfSettled();
	void UpdateLeanStep();
	void UpdateCameraFOV(float DeltaSeconds);
	float GetEffectiveFirstPersonAimAlpha() const;
	float GetLookSensitivityScale() const;

	bool CanStartSlide() const;
	void StopSlide(ESlideEndReason EndReason);
	void HandleSlideWallHit(const FHitResult& Hit);
	void BeginSlideCameraEffect(float CameraRollDegrees, float Duration);
	void EndSlideCameraEffect();

	void Die();
	void HandleDeath();

	FGameplayTag ResolveShooterConditionTag() const;
	void BroadcastPartnerShieldState();
	void RefreshUIForRespawn();

	FName ResolveMontageSectionNameForWeapon(EWeaponType WeaponType) const;
	void PlayFirstPersonMontage(UAnimMontage* Montage);
	void PlayFirstPersonMontageForWeapon(UAnimMontage* Montage, EWeaponType WeaponType, bool bUseWeaponSection = true);
	void PlayThirdPersonMontage(UAnimMontage* Montage);
	void PlayThirdPersonMontageForWeapon(UAnimMontage* Montage, EWeaponType WeaponType, bool bUseWeaponSection = true);
	void PlayFirstPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType);
	void PlayThirdPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType);
	void StopFirstPersonMontage(UAnimMontage* Montage);
	void StopThirdPersonMontage(UAnimMontage* Montage);
	void StopSplitMontages(UAnimMontage* FirstPersonMontage, UAnimMontage* ThirdPersonMontage);
	void PlayEquipMontages();
	const UAnimMontage* GetFirstPersonReloadMontage() const { return FirstPersonReloadMontage; }
	const UAnimMontage* GetFirstPersonEquipMontage() const { return FirstPersonEquipMontage; }
	void ClearInputIntent();

	void CleanupOwnedWeapons();
};
