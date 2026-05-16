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
class UPartnerMovementComponent;
class UPartnerSupportComponent;
class UPartnerCombatComponent;

UCLASS()
class OUTLIER_API APartnerCharacter : public AFirstPersonCharacter
{
	GENERATED_BODY()

	friend class UPartnerMovementComponent;
	friend class UPartnerSupportComponent;
	friend class UPartnerCombatComponent;

protected:
	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerMovementComponent> MovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerSupportComponent> SupportComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerCombatComponent> CombatComponent;

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

	// Skill Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ScanRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ScanDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ScanCooldown = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ScanExpandSpeed = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float HackRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float HackDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float HackCooldown = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float AreaOfEffectRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float AreaOfEffectDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float AreaOfEffectCooldown = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ShieldRange = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ShieldDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ShieldCooldown = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill")
	float ShieldAmount = 200.0f;

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
	float AssistInterpSpeed = 400.0f;

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

	uint8 bIsAccelerate : 1 = false;
	uint8 bPartnerDataInitialized : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	uint8 bIsRebooting : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
	uint8 bIsInvincible : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
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

	// Timers
	FTimerHandle BoundaryCheckTimerHandle;

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

	void UpdateBoundaryByTimer();
	void HandlePartnerHit();
	void StartReboot();
	void FinishReboot();
	void ClearHitInvincible();
	void ClearRebootInvincible();
	bool CanAcceptInput() const;

	void SetMoveMode(EPartnerMoveMode NewMode);
	void ApplyMoveMode(EPartnerMoveMode NewMode);
	bool CanApplyMoveMode(EPartnerMoveMode NewMode) const;

	UFUNCTION(Server, Reliable)
	void ServerSetMoveMode(EPartnerMoveMode NewMode);

	UFUNCTION(Server, Reliable)
	void ServerUseSkill(EPartnerSkillType SkillType);

	UFUNCTION(Server, Reliable)
	void ServerHackTarget(AActor* TargetActor);

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

	void SetShooterCharacter(AShooterCharacter* NewShooter);
	
	UFUNCTION(Client, Reliable)
	void ClientNotifySkillUseResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result);

	float GetCurrentInertialCameraPitchDegrees() const;
	float GetCurrentInertialCameraRollDegrees() const;
	float GetMaxInertialCameraPitchDegrees() const { return FMath::Max(CameraPitchOnMove, 0.0f); }
	float GetMaxInertialCameraRollDegrees() const { return FMath::Max(CameraRollOnTurn, 0.0f); }

	void SetMovementState(EDroneMovementState State);
};
