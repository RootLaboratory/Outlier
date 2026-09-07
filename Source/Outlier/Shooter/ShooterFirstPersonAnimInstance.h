// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "Shooter/Anim/FirstPersonProceduralAnimRuntime.h"
#include "Weapon/WeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterFirstPersonAnimInstance.generated.h"

class UProceduralAnimValues;
class AWeaponBase;
class UAnimSequenceBase;
struct FWeaponValues;

/**
 * 
 */
UCLASS()
class OUTLIER_API UShooterFirstPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;


	UFUNCTION(BlueprintCallable, Category = "Anim|FP Procedural|Recoil")
	void AddViewModelRecoil(float GameplayRecoilScale = 1.0f);

	void AddViewModelRecoil(float GameplayRecoilScale, const FVector2D& NormalizedShotDirection);

	float GetViewModelAimAlpha() const { return ViewModelAimAlpha; }
	float GetViewModelSprintAlpha() const { return ViewModelSprintAlpha; }

	// 1인칭 자체 트레이스로 계산한 근접 단계 raw 타깃. 3인칭이 이 값을 자기
	// WallTight 타깃과 union해서(FP → TP 단방향) 1인칭이 HardStop/VeryClose에
	// 들어가면 Tight도 같이 걸린다. 보간된 알파가 아니라 raw 타깃만 넘길 것
	float GetWallVeryCloseOwnTargetAlpha() const { return WallVeryCloseOwnTargetAlpha; }
	float GetWallHardStopOwnTargetAlpha() const { return WallHardStopOwnTargetAlpha; }

	void UpdateViewModelRecoil(float DeltaSeconds);

	// 반동 스프링 보간 상태(프레임 간 속도 유지). FRecoilValues의 Stiffness/Mass/Damping/TargetVelocity로 구동.
	FVectorSpringState RecoilLocSpringState;
	FVectorSpringState RecoilRotSpringState;

protected:
	UFUNCTION()
	void HandleOwnerDeath();

	void UpdateFirstPersonProceduralValues(float DeltaSeconds);
	void UpdateFirstPersonProceduralRuntime(float DeltaSeconds);
	void UpdateFirstPersonDiagnostics(float DeltaSeconds, bool bWeaponChanged);
	void LogFirstPersonDiagnostics(const TCHAR* Reason) const;
	void UpdateWallOffset(float DeltaSeconds, const FWeaponValues* WeaponValues);
	bool IsMontageInProceduralActionWindow(const UAnimMontage* Montage, float EarlyReleaseTime) const;

	// 벽 근접 ADS 해제 알파를 얻는다. 3인칭 인스턴스가 단일 기준으로 계산한
	// 값을 우선 사용하고, 몸 메시에 인스턴스가 없으면 로컬 계산으로 폴백
	float ResolveWallAimBreakAlpha(const FWeaponValues* WeaponValues) const;

	// 손가락 디테일: 무작위 간격으로 짧은 펄스를 발생시켜 살아있는 느낌을 준다
	void UpdateFingerMovement(float DeltaSeconds, const FWeaponValues& WeaponValues, bool bCanPlayIdleDetail);

	// 벽 오프셋 상태 전체 초기화 (전제 조건이 깨진 조기 반환 경로에서 사용).
	// bResetPoseAssets: 회피 포즈 애셋 참조까지 해제할지 여부
	void ResetWallOffsetState(bool bResetPoseAssets);

protected:
	// AnimGraph 연결을 변경하지 않고 Procedural 레이어를 격리하기 위한 튜닝용 토글.
	// 기본값은 모두 true여서 기존 런타임 동작을 유지한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle")
	bool bEnableProceduralAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralHip = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralAim = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralIdle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralLeftHandIK = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralSway = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralSprint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralRecoil = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralAction = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Debug Toggle", meta = (EditCondition = "bEnableProceduralAnimation"))
	bool bEnableProceduralWallOffset = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Speed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Direction = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimYaw = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimPitch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float FirstPersonAimPitchScale = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float FirstPersonAimPitchClamp = 35.0f;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSliding : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsEquipping : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsDead : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsLean : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsGrounded : 1 = true;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsInAir : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Weapon")
	TObjectPtr<AWeaponBase> CurrentWeapon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Weapon")
	TObjectPtr<const UProceduralAnimValues> CurrentProceduralValues = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AWeaponBase> PreviousWeapon = nullptr;

	UPROPERTY(Transient)
	uint8 bWasSprintingLastFrame : 1 = false;

	UPROPERTY(Transient)
	uint8 bBlockStartStopThisFrame : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	uint8 bShouldMove : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	uint8 bIsFalling : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	uint8 bIsRunOrSprint : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Hip")
	FVector ViewModelHipPoseLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Hip")
	FRotator ViewModelHipPoseRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Idle")
	float ViewModelIdleIntensity = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	FVector ViewModelSprintPoseLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	FRotator ViewModelSprintPoseRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	float ViewModelSprintAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	float ViewModelNonSprintProceduralAlpha = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	float ViewModelSprintExitDetailAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Sprint")
	float SprintExitDetailBlockTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Sprint")
	float SprintExitDetailBlendSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|StartStop")
	float StartStopDisableBlendSpeed = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sprint")
	float SprintAnimMultiplier = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FVector ViewModelLeftHandIKLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FRotator ViewModelLeftHandIKRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FVector ViewModelLeftHandGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FRotator ViewModelLeftHandGripOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FVector ViewModelLeftHandActionReturnGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FRotator ViewModelLeftHandActionReturnGripOffsetRot = FRotator::ZeroRotator;

	FVector LastLeftHandActionGripOffsetLoc = FVector::ZeroVector;
	FRotator LastLeftHandActionGripOffsetRot = FRotator::ZeroRotator;
	float PreviousLeftHandActionIKAlpha = 0.0f;
	float LeftHandActionReturnTimer = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	float ViewModelLeftHandIKAlpha = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Lean")
	FRotator ViewModelLeanRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Lean")
	float ViewModelLeanAlpha = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Weapon")
	float ViewModelWeaponPoseAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon")
	float WeaponPoseEquipInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeaponPoseEquipInitialAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon")
	float WeaponPoseUnequipInterpSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon")
	float WeaponDetailAlphaStart = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon")
	float WeaponDetailAlphaEnd = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Weapon")
	float ViewModelWeaponEquipDetailAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|Weapon")
	float WeaponEquipDetailDelayAlpha = 0.75f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Movement")
	FVector ViewModelMovementLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Movement")
	FRotator ViewModelMovementRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Movement")
	float FingerMovementAlpha = 0.0f;

	UPROPERTY(Transient)
	float FingerMovementPulseTime = 0.0f;

	UPROPERTY(Transient)
	float FingerMovementCooldownTime = 0.0f;

	UPROPERTY(Transient)
	uint8 bFingerMovementPulseActive : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Recoil")
	FVector ViewModelRecoilLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Recoil")
	FRotator ViewModelRecoilRot = FRotator::ZeroRotator;

	// Walk Cycle
	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Walk")
	float WalkCycleTime = 0.0f;

	// Sway
	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sway")
	FRotator PrevAimRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sway")
	FRotator TargetSwayRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sway")
	FVector ViewModelSwayLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Sway")
	FRotator ViewModelSwayRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Jump")
	FVector ViewModelJumpLandLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Jump")
	FRotator ViewModelJumpLandRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Jump")
	float ViewModelJumpLandAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Aim")
	float ViewModelAimAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Aim")
	FVector ViewModelAimPoseLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Aim")
	FRotator ViewModelAimPoseRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FVector ViewModelLeftHandJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FVector ViewModelStandLeftHandJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FVector ViewModelLeftUpperArmPitchLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelLeftUpperArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelLeftLowerArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FVector ViewModelStandLeftUpperArmPitchLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelStandLeftUpperArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelStandLeftLowerArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Aim")
	float ReloadAimAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Reload")
	float ViewModelReloadPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Reload")
	float ViewModelReloadIKBlendAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Fire")
	float ViewModelFireIKAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Equip")
	float ViewModelEquipPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Equip")
	float ViewModelEquipIKBlendAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Slide")
	float ViewModelSlidePoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Slide")
	float ViewModelSlideIKBlendAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	uint8 bIsForwardWalk : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	FVector ViewModelForwardWalkLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	FRotator ViewModelForwardWalkRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	float ViewModelWalkAnimAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	FVector ViewModelForwardWalkAnimLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Tilt")
	FRotator ViewModelForwardWalkAnimRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Crouch")
	float CrouchAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallOffsetAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallMuzzleBlockAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	FVector ViewModelWallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	FRotator ViewModelWallOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	FVector WallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	FRotator WallOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	TObjectPtr<UAnimSequenceBase> WallAvoidUpPose = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	TObjectPtr<UAnimSequenceBase> WallAvoidDownPose = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallAvoidUpAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallAvoidDownAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallAvoidSideAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	float WallAvoidSideSign = 0.0f;

	float WallTargetAlpha = 0.0f;
	float WallSmoothedTargetAlpha = 0.0f;
	float WallUpTargetAlpha = 0.0f;
	float WallUpSmoothedTargetAlpha = 0.0f;
	float WallDownTargetAlpha = 0.0f;
	float WallDownSmoothedTargetAlpha = 0.0f;
	float WallSideTargetAlpha = 0.0f;
	float WallSideSmoothedTargetAlpha = 0.0f;
	float WallSideTargetSign = 0.0f;
	float WallMuzzleBlockTargetAlpha = 0.0f;
	float WallMuzzleBlockSmoothedTargetAlpha = 0.0f;
	float WallVeryCloseTargetAlpha = 0.0f;
	float WallVeryCloseOwnTargetAlpha = 0.0f;
	float WallVeryCloseSmoothedTargetAlpha = 0.0f;
	float WallVeryCloseAlpha = 0.0f;
	float WallTopEdgeTargetAlpha = 0.0f;
	float WallTopEdgeAlpha = 0.0f;
	float WallCeilingTargetAlpha = 0.0f;
	float WallCeilingAlpha = 0.0f;
	float WallHardStopTargetAlpha = 0.0f;
	float WallHardStopOwnTargetAlpha = 0.0f;
	float WallHardStopSmoothedTargetAlpha = 0.0f;
	float WallHardStopAlpha = 0.0f;
	float WallHardStopSafetyAlpha = 0.0f;
	float WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
	float WallMuzzleBlockDownPreferenceAlpha = 0.0f;
	float WallMuzzleBlockReleaseHoldTimer = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftRightWalk")
	float StrafeWalkAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftRightWalk")
	FRotator ViewModelCurrentStrafeWalkRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|StartStop")
	FVector ViewModelStartStopLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|StartStop")
	FRotator ViewModelStartStopRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Runtime")
	FFirstPersonProceduralAnimRuntime ViewModelProceduralRuntime;

	float StartStopTime = 0.0f;
	float StartStopDuration = 0.15f;
	float SprintExitDetailBlockTimer = 0.0f;
	float FirstPersonDiagnosticLogTimeRemaining = 0.0f;
	int32 StartStopDirection = 0;
	uint8 bWasShouldMove : 1 = false;
	uint8 bWasSprinting : 1 = false;
	uint8 bHadWeaponPose : 1 = false;
};
