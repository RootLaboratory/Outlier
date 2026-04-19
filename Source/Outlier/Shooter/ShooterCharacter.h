// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "ShooterCharacter.generated.h"

class UInputAction;
struct FInputActionValue;
class UShooterInputConfig;
class AWeaponBase;
class USceneCaptureComponent2D;
class UShooterHealthComponent;
class UShooterInventoryComponent;
class UShooterCombatComponent;
class UShooterMovementComponent;
enum class EWeaponType : uint8;
class UAnimMontage;
class UCurveFloat;

UENUM(BlueprintType)
enum class EMovementState : uint8
{
	Idle,
	Walk,
	Run,
	Sit,
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

/**
 * 
 */
UCLASS(abstract)
class OUTLIER_API AShooterCharacter : public AFirstPersonCharacter
{
	GENERATED_BODY()

	friend class UShooterHealthComponent;
	friend class UShooterInventoryComponent;
	friend class UShooterCombatComponent;
	friend class UShooterMovementComponent;

protected:
	/** Input Config */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Input)
	TObjectPtr<UShooterInputConfig> InputConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TObjectPtr<AWeaponBase>> OwnedWeapons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimMontage> SlideMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Health)
	float MaxHP = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_CurHP, EditAnywhere, BlueprintReadWrite, Category = Health)
	float CurHP = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	float CurrentLeanValue = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = Movement)
	float WalkSpeed = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = Movement)
	float SprintSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, Category = Status)
	float InteractRange = 100.0f;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsAiming : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSliding : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSlidingCanceled : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSuitMenuOpen : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Status)
	uint8 bIsDead : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	int32 SelectedSuitIndex = 0; // 이후에 Suit 관련 만들면서 거기에 있는 enum 값으로 교체?

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterInventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShooterMovementComponent> MovementComponent;

	UPROPERTY(ReplicatedUsing = OnRep_MovementState, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EMovementState MovementState = EMovementState::Idle;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EWeaponMode WeaponMode = EWeaponMode::None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	ECombatState CombatState = ECombatState::Idle;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	uint8 bIsReloading : 1= false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	uint8 bSprintHeld : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	uint8 bCrouchHeld : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	uint8 bAimHeld : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	uint8 bFireHeld : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	uint8 bSecondaryOnCooldown : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	uint8 bIsMeleeAttacking : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State")
	uint8 bIsEquipping : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	float TargetLeanValue = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Combat)
	float LeanInterpSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Combat)
	float MaxLeanAngle = 15.0f;

	FTimerHandle LeanUpdateTimerHandle;

	FRotator BaseFirstPersonCameraRootRotation = FRotator::ZeroRotator;
	FRotator BaseFirstPersonMeshRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideDuration = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float MinSlideSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideWallStopDotValue = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	float SlideSpeedMultiplier = 1.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide")
	TObjectPtr<UCurveFloat> SlideSpeedCurve;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	FVector SlideDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideStartSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	float SlideElapsedTime = 0.0f;

	FTimerHandle SlideUpdateTimerHandle;

	FTimerHandle SlideTimerHandle;
	FTimerHandle SecondaryCooldownStateTimerHandle;
public:

	/** First Person Mesh Default Weapon Socket */
	FName FirstPersonWeaponSocketDefault = FName("HandGrip_R");

	/** Third Person Mesh Default Weapon Socket */
	FName ThirdPersonWeaponSocketDefault = FName("HandGrip_R");

	/** First Person Mesh Rifle Weapon Socket */
	FName FirstPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	/** Third Person Mesh Rifle Weapon Socket */
	FName ThirdPersonWeaponSocketRifle = FName("HandGrip_R_Rifle");

	/** First Person Mesh Pistol Weapon Socket */
	FName FirstPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_FP");

	/** Third Person Mesh Pistol Weapon Socket */
	FName ThirdPersonWeaponSocketPistol = FName("HandGrip_R_Pistol_TP");

	FName GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const;

	FName GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const;
	
	/** Constructor */
	AShooterCharacter();

protected:
	virtual void BeginPlay() override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	virtual void Landed(const FHitResult& Hit) override;

	virtual void OnMovementModeChanged(EMovementMode  PrevMovementMode, uint8 PreviousCustomMode) override;

public:
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnCharacterDeath OnCharacterDeath;

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnMovementStateChanged OnMovementStateChanged;

protected:
	void StartLeanUpdate();
	void StopLeanUpdateIfSettled();
	void UpdateLeanStep();

	virtual void TryStartAttack() override;

	virtual void TryStopAttack() override;

	void TryReload();

	void TrySwitchWeapon1();

	void TrySwitchWeapon2();

	void TrySwitchWeapon3();

	void SelectWeaponByIndex(int32 SlotIndex);

	void TryStartAim();

	void TryStopAim();

	void TryStartSprint();

	void TryStopSprint();

	void TryStartCrouchOrSlide();

	void TryStopCrouch();

	void TryInteract();

	void TryOpenSuitMenu();

	void TryCloseSuitMenu();

	void UpdateSuitSelection(const FInputActionValue& Value);

	void TryUseSuit();

	void TrySlide();

	void TryLean(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* TargetActor);

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
	void ServerStartCrouchOrSlide();

	UFUNCTION(Server, Reliable)
	void ServerStopCrouch();

	UFUNCTION(Server, Reliable)
	void ServerJumpStart();

	UFUNCTION(Server, Reliable)
	void ServerJumpEnd();

	void Die();

public:

	UFUNCTION()
	void OnRep_CurHP();

	UFUNCTION()
	void OnRep_MovementState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void EquipWeapon(AWeaponBase* Weapon) override;

	void ApplyDamageInternal(float DamageAmount);

	float GetAimYawForAnimation() const;

	float GetAimPitchForAnimation() const;

	bool CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const;
	bool CanAimInCurrentState() const;
	bool CanReloadInCurrentState() const;
	bool CanFireInCurrentState() const;

	bool IsAiming() const { return bIsAiming; }

	bool IsSliding() const { return bIsSliding; }

	bool IsSprinting() const { return bIsSprinting; }

	bool IsSlidingCanceled() const { return bIsSlidingCanceled; }

	void RefreshMovementState();
	void RefreshCombatState();
	void RefreshWeaponMode();
	void ResolveStateConflicts();
	void SetMovementStateImmediate(EMovementState NewState);

	void StopSprintInternal();
	void StopAimInternal();
	void BeginReloadInternal();
	void CancelReloadInternal();
	void FinishReloadInternal();
	void BeginSecondaryCooldownInternal(float CooldownDuration);
	void FinishSecondaryCooldownInternal();

	bool CanStartSlide() const;
	void StopSlide(ESlideEndReason EndReason);
	void HandleSlideWallHit(const FHitResult& Hit);

	void HandleDeath();
	void UpdateLocalHealthUI() const;
	void PlayLocalActionMontage(UAnimMontage* Montage);

public:
	UFUNCTION(BlueprintPure)
	float GetCurrentLeanAlpha() const { return CurrentLeanValue; }

	UFUNCTION(BlueprintPure)
	EMovementState GetMovementState() const { return MovementState; }

	UFUNCTION(BlueprintPure)
	ECombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure)
	EWeaponMode GetWeaponMode() const { return WeaponMode; }

	UFUNCTION(BlueprintPure)
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure)
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintCallable, Category = "Animation|Notify")
	void HandleReloadCommitNotify();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();
};
