// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "Interface/WeaponMuzzleProvider.h"
#include "AbilitySystemInterface.h"
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
class UPartnerAbilityComponent;
class UPartnerEMPComponent;
class UPartnerSpriteAnimationComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UOutlierAbilitySystemComponent;
class UOutlierVitalAttributeSet;
class UPartnerVitalityComponent;

class USceneComponent;
UCLASS()
class OUTLIER_API APartnerCharacter : public AFirstPersonCharacter, public IWeaponMuzzleProvider, public IAbilitySystemInterface
{
	GENERATED_BODY()

	friend class UPartnerMovementComponent;
	friend class UPartnerSupportComponent;
	friend class UPartnerCombatComponent;
	friend class UPartnerDistanceComponent;
	friend class UPartnerAbilityComponent;
	friend class UPartnerHackComponent;
	friend class UPartnerEMPComponent;
protected:
	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UOutlierAbilitySystemComponent> OutlierAbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UOutlierVitalAttributeSet> VitalAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UPartnerVitalityComponent> PartnerVitalityComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerDistanceComponent> DistanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPartnerSpriteAnimationComponent> FaceSpriteAnimationComponent;

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
	TObjectPtr<USceneComponent> ThirdPersonTiltRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> FirstPersonWeaponRoot;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachment")
	FName FirstPersonWeaponAttachSocketName = TEXT("FirstPerson");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachment")
	FName ThirdPersonWeaponAttachSocketName = TEXT("ThirdPerson");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Test|Weapon")
	FKey ToggleTestWeaponAttachmentKey = EKeys::T;

	// Partner 무기는 본체 메시와 일체형이므로 Weapon Actor 대신 이 소켓에서 총구 연출을 시작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation")
	FName FirstPersonWeaponMuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation")
	FName ThirdPersonWeaponMuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	TObjectPtr<UNiagaraSystem> BoostVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	FName BoostVFXSocketPrefix = TEXT("Boost");

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

	FTimerHandle BoostNoiseTimerHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise|Boost", meta = (ClampMin = "0.05"))
	float BoostNoiseInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise|Boost", meta = (ClampMin = "0.0"))
	float BoostNoiseLoudness = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise|Boost", meta = (ClampMin = "0.0"))
	float BoostNoiseMaxRange = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise|Boost", meta = (ClampMin = "0.0"))
	float BoostNoiseMinimumSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise|Boost")
	FName BoostNoiseTag = TEXT("Boost");

	UPROPERTY(ReplicatedUsing = OnRep_IsAccelerate, VisibleAnywhere, BlueprintReadOnly, Category = "Move")
	uint8 bIsAccelerate : 1 = false;
	uint8 bPartnerDataInitialized : 1 = false;

	// Shooter 은신 토글과 함께 AI 감지 대상에서 제외하기 위한 테스트 상태다.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Test|Stealth")
	uint8 bTestStealthed : 1 = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Partner|EnemyPossession")
	uint8 bHiddenForEnemyPossession : 1 = false;

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
	FDataTableRowHandle VitalityDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	FDataTableRowHandle PartnerCameraAssistDataRow;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> BoostVFXComponents;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator,
		AActor* DamageCauser
	) override;

	virtual void UnPossessed() override;
	void RefreshAbilitySystemActorInfo();
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void DoMove(float Right, float Forward) override;
	virtual void OnMoveInputUpdated(const FVector2D& MoveValue);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// AFirstPersonCharacter의 AttackAction 입력 진입점.
	// 실제 권한 처리와 무기 호출은 CombatComponent의 공개 공격 API에 위임한다.
	virtual void TryStartAttack() override;
	virtual void TryStopAttack() override;


	void AreaOfEffect();
	void CameraAssist();
	void StopCameraAssist();
	void TryHacking();
	void EndHacking();
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
	void ToggleTestWeaponEquipment();

	void SetBoundaryOutside(bool bOutside);
	EPartnerBoundaryState GetBoundaryOutside();

	UPartnerHackComponent* GetRuntimeHackComponent() const;
	UPartnerEMPComponent* GetRuntimeEMPComponent() const;

	void SetMoveMode(EPartnerMoveMode NewMode);
	void ApplyMoveMode(EPartnerMoveMode NewMode);
	bool CanApplyMoveMode(EPartnerMoveMode NewMode) const;
	void ApplyAccelerateState(bool bNewAccelerate);
	void StartBoostNoiseTimer();
	void StopBoostNoiseTimer();
	void ReportBoostNoise();
	void AttachBoostVFXToMeshes();
	void AttachBoostVFXToMesh(USkeletalMeshComponent* MeshComponent);
	void CleanupBoostVFXComponents();

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
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGameplayTagContainer GetOwnedGameplayTagsForQuery() const override;
	UOutlierAbilitySystemComponent* GetOutlierAbilitySystemComponent() const
	{
		return OutlierAbilitySystemComponent;
	}
	const UOutlierVitalAttributeSet* GetVitalAttributeSet() const { return VitalAttributeSet; }
	UPartnerVitalityComponent* GetPartnerVitalityComponent() const { return PartnerVitalityComponent; }
	bool CanAcceptInput() const;
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnDroneMovementStateChanged OnDroneMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnPartnerSkillUseResult OnPartnerSkillUseResult;

public:
	APartnerCharacter();

	virtual USkeletalMeshComponent* GetWeaponMuzzleComponent(bool bFirstPerson) const override;

	virtual FName GetWeaponMuzzleSocketName(bool bFirstPerson) const override
	{
		return bFirstPerson ? FirstPersonWeaponMuzzleSocketName : ThirdPersonWeaponMuzzleSocketName;
	}

	FName GetFirstPersonWeaponAttachSocketName() const { return FirstPersonWeaponAttachSocketName; }
	FName GetThirdPersonWeaponAttachSocketName() const { return ThirdPersonWeaponAttachSocketName; }
	USceneComponent* GetFirstPersonWeaponRoot() const { return FirstPersonWeaponRoot; }

	UFUNCTION()
	void OnRep_DroneMovementState();

	UFUNCTION()
	void OnRep_MoveMode();

	UFUNCTION()
	void OnRep_IsAccelerate();

	void SetShooterCharacter(AShooterCharacter* NewShooter);
	void SetTestStealthed(bool bNewStealthed);
	
	UFUNCTION(Client, Reliable)
	void ClientNotifySkillUseResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result);

	float GetCurrentInertialCameraPitchDegrees() const;
	float GetCurrentInertialCameraRollDegrees() const;
	USceneComponent* GetThirdPersonTiltRoot() const { return ThirdPersonTiltRoot; }
	float GetMaxInertialCameraPitchDegrees() const { return FMath::Max(CameraPitchOnMove, 0.0f); }
	float GetMaxInertialCameraRollDegrees() const { return FMath::Max(CameraRollOnTurn, 0.0f); }

	void SetMovementState(EDroneMovementState State);
	void NotifyBoundaryUI(bool bDisabled);
	void ApplyDamagedEvent(float InRatio) const;
	void NullifyDamagedEvenet() const;

	void SetEnemyPossessionProtection(bool bEnabled);
	void StopActionsForReboot();
	void RefreshEnemyDetectionForVitality();

	// 입력뿐 아니라 Ability/BP에서도 사용할 수 있는 Partner 공격 API.
	// 소유 클라이언트에서 호출하면 CombatComponent가 서버 RPC로 전달한다.
	UFUNCTION(BlueprintCallable, Category = "Partner|Combat")
	void StartWeaponAttack();

	UFUNCTION(BlueprintCallable, Category = "Partner|Combat")
	void StopWeaponAttack();

	void HandleAutoReloadRequested();
};
