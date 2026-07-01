// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Engine/DataTable.h"
#include "PartnerCharacter.generated.h"

UENUM(BlueprintType)
enum class EDroneMovementState : uint8
{
	Follow,
	Fly
};

UENUM(BlueprintType)
enum class EPartnerMoveMode : uint8
{
	Normal,
	FreeMove,
	SyncMove,
	CameraAssist
};

UENUM(BlueprintType)
enum class EPartnerBoundaryState : uint8
{
	Inside,
	Outside
};

UENUM(BlueprintType)
enum class EPartnerSkillType : uint8
{
	Shield,
	Scan,
	Hack,
	AreaOfEffect
};

UENUM(BlueprintType)
enum class EPartnerSkillUseResult : uint8
{
	Success,
	InvalidState,
	Cooldown,
	NoTarget,
	OutOfRange,
	NoLineOfSight
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroneMovementStateChanged, EDroneMovementState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartnerSkillUseResult, EPartnerSkillType, SkillType, EPartnerSkillUseResult, Result);

class AShooterCharacter;
class UCurveFloat;
class UPartnerDistanceComponent;
class UPartnerMovementComponent;
class UPartnerSupportComponent;
class UPartnerCombatComponent;
class UPartnerHackComponent;
class USceneCaptureComponent2D;
class UPartnerAbilityComponent;
class UPartnerEMPComponent;
UCLASS()
class OUTLIER_API APartnerCharacter : public AFirstPersonCharacter
{
	GENERATED_BODY()

	friend class UPartnerMovementComponent;
	friend class UPartnerSupportComponent;
	friend class UPartnerCombatComponent;
	friend class UPartnerDistanceComponent;
	friend class UPartnerAbilityComponent;
protected:
	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerDistanceComponent> DistanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerMovementComponent> MovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerSupportComponent> SupportComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TestAbilityComponents")
	TObjectPtr<UPartnerAbilityComponent> TestAbilityComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerEMPComponent> EMPComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

	// Move Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float BoostSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float VerticalSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float Acceleration = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float Deceleration = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float SyncMoveDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float SyncMoveInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float CameraAssistStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move")
	float CameraAssistInterpSpeed = 12.0f;

	// Control Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float LookSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float PitchMin = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float PitchMax = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float LookInputDeadZone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float TurnInterpSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float RotationLagAmount = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float RotationLagRecoverSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float CameraPitchOnMove = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float CameraRollOnTurn = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float CameraRollInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float MeshInertialTiltScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float CameraInertialTiltScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float ViewModelInertialTiltScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float InertialTiltReboundRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float InertialTiltReboundMaxScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float InertialTiltReboundInterpMultiplier = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	float InertialTiltRecoverInterpMultiplier = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	TSoftObjectPtr<UCurveFloat> TurnFeelCurve;

	// Skill Data - Scan
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Scan")
	float ScanRange = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Scan")
	float ScanDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Scan")
	float ScanCooldown = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Scan")
	float ScanExpandSpeed = 5.0f;

	
	// Skill Data - Hack
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Hack")
	float HackRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Hack")
	float HackEffectiveRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Hack")
	float HackMiniGameTime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Hack")
	float HackCooldown = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Hack")
	float HackFailPenaltyTime = 3.0f;

	// Skill Data - EMP
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|EMP")
	float AreaOfEffectRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|EMP")
	float EMPMarkingTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|EMP")
	float EMPStunDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|EMP")
	float AreaOfEffectCooldown = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|EMP")
	int32 EMPMaxTargets = 99;

	// Skill Data - Shield
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldCooldown = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldAmount = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldDecayRate = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Shield")
	float ShieldDecayDelay = 1.0f;

	// Skill Data - Interaction
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill|Interaction")
	float InteractionRange = 200.0f;

	// Skill Common
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float CoolDown = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float Duration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float CastTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	uint8 bRequireLineOfSight : 1 = true;

	// Boundary Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boundary")
	float SuitDisableBoundaryRadius = 500.0f;

	// CameraAssist Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistTargetOffset = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	FVector AssistTargetLocalOffset = FVector(0.0f, 0.0f, 50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistMinDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistMaxDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistMaxAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistDeadZoneAngle = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistInterpSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraAssist")
	float AssistStrength = 100.0f;

	// Survival Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival")
	int32 MaxHitCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival")
	float RebootTime = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival")
	float InvincibleAfterRebootTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival")
	float HitInvincibleTime = 0.25f;

	// Replicated 
	UPROPERTY(ReplicatedUsing = OnRep_DroneMovementState, VisibleAnywhere, BlueprintReadOnly, Category = "State")
	EDroneMovementState MovementState = EDroneMovementState::Follow;

	UPROPERTY(ReplicatedUsing = OnRep_MoveMode, VisibleAnywhere, BlueprintReadOnly)
	EPartnerMoveMode MoveMode = EPartnerMoveMode::Normal;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly)
	EPartnerBoundaryState BoundaryState = EPartnerBoundaryState::Inside;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly)
	FVector SyncLocalOffset = FVector::ZeroVector;

	UPROPERTY(Replicated)
	uint8 bShieldActive : 1;

	UPROPERTY(Replicated)
	uint8 bScanning : 1;

	UPROPERTY(Replicated)
	float LastHackServerTime = -999.0f;

	FTimerHandle RebootTimerHandle;
	FTimerHandle HitInvincibleTimerHandle;
	FTimerHandle RebootInvincibleTimerHandle;

	UPROPERTY(ReplicatedUsing = OnRep_IsAccelerate, VisibleAnywhere, BlueprintReadOnly, Category = "Move")
	uint8 bIsAccelerate : 1 = false;
	uint8 bPartnerDataInitialized : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	uint8 bIsRebooting : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	uint8 bIsInvincible : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHitCount, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	int32 CurrentHitCount = 0;

	// DataTable
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle DroneMoveDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle DroneControlDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle PartnerSkillCommonDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle PartnerSkillDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle PartnerSurvivalDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle PartnerCameraAssistDataRow;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter;

protected:
	virtual void BeginPlay() override;
	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator,
		AActor* DamageCauser
	) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void DoMove(float Right, float Forward) override;
	virtual void OnMoveInputUpdated(const FVector2D& MoveValue);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TryStartAttack() override;
	virtual void TryStopAttack() override;


	void AreaOfEffect();
	void CameraAssist();
	void StopCameraAssist();
	void TryHacking();
	void Hacking(AActor* TargetActor);
	void TryEMP();
	//
	void TestAbilityScan();
	void Scan();
	void Shield();
	void SyncMove();
	void StopSyncMove();
	void ToggleAccelerate();
	void FreeMove();
	void StopFreeMove();
	void VerticalMove(const FInputActionValue& Value);
	void StopVerticalMove();

	void SetBoundaryOutside(bool bOutside);
	EPartnerBoundaryState GetBoundaryOutside();

	void StartReboot();
	void FinishReboot();
	void ClearHitInvincible();
	void ClearRebootInvincible();
	bool CanAcceptInput() const;
	UPartnerHackComponent* GetRuntimeHackComponent() const;
	UPartnerEMPComponent* GetRuntimeEMPComponent() const;

	void SetMoveMode(EPartnerMoveMode NewMode);
	void ApplyMoveMode(EPartnerMoveMode NewMode);
	bool CanApplyMoveMode(EPartnerMoveMode NewMode) const;
	void ApplyAccelerateState(bool bNewAccelerate);

	UFUNCTION(Server, Reliable)
	void ServerSetMoveMode(EPartnerMoveMode NewMode);

	UFUNCTION(Server, Reliable)
	void ServerSetAccelerate(bool bNewAccelerate);

	UFUNCTION(Server, Reliable)
	void ServerUseSkill(EPartnerSkillType SkillType);

	void EnsurePartnerDataInitialized();
	void InitializeFromDataTables();

	virtual void LookInput(const FInputActionValue& Value) override;
public:
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnDroneMovementStateChanged OnDroneMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnPartnerSkillUseResult OnPartnerSkillUseResult;

public:
	APartnerCharacter();

	UFUNCTION()
	void OnRep_DroneMovementState();

	UFUNCTION()
	void OnRep_MoveMode();

	UFUNCTION()
	void OnRep_IsAccelerate();

	UFUNCTION()
	void OnRep_CurrentHitCount();

	void SetShooterCharacter(AShooterCharacter* NewShooter);
	
	UFUNCTION(Client, Reliable)
	void ClientNotifySkillUseResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result);

	float GetCurrentInertialCameraPitchDegrees() const;
	float GetCurrentInertialCameraRollDegrees() const;
	float GetMaxInertialCameraPitchDegrees() const { return FMath::Max(CameraPitchOnMove, 0.0f); }
	float GetMaxInertialCameraRollDegrees() const { return FMath::Max(CameraRollOnTurn, 0.0f); }

	void SetMovementState(EDroneMovementState State);
	void NotifyBoundaryUI(bool bDisabled);
	void ApplyDamagedEvent(float InRatio) const;
	void NullifyDamagedEvenet() const;

	void HandlePartnerHit();

};
