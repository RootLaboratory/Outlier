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

	void UpdateViewModelRecoil(float DeltaSeconds);

	// 반동 스프링 보간 상태(프레임 간 속도 유지). FRecoilValues의 Stiffness/Mass/Damping/TargetVelocity로 구동.
	FVectorSpringState RecoilLocSpringState;
	FVectorSpringState RecoilRotSpringState;

protected:
	UFUNCTION()
	void HandleOwnerDeath();

	void UpdateFirstPersonProceduralValues(float DeltaSeconds);
	void UpdateFirstPersonProceduralRuntime();

protected:
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
	uint8 bIsDead : 1 = false;

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

	UPROPERTY(Transient)
	uint8 bSkipLeftHandExtraOffsetThisFrame : 1 = false;

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

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FVector ViewModelLeftHandIKLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FRotator ViewModelLeftHandIKRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FVector ViewModelLeftHandGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	FRotator ViewModelLeftHandGripOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|LeftHand")
	float ViewModelLeftHandIKAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|LeftHand|Debug")
	uint8 bDebugLeftHandIK : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|FP Procedural|LeftHand|Debug", meta = (ClampMin = "0.05"))
	float LeftHandIKDebugLogInterval = 0.35f;

	UPROPERTY(Transient)
	float LeftHandIKDebugLogTime = 0.0f;

	UPROPERTY(Transient)
	FVector DebugLastLeftHandIKTargetLoc = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator DebugLastLeftHandIKTargetRot = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	float DebugLastLeftHandIKTargetAlpha = 0.0f;

	UPROPERTY(Transient)
	uint8 bDebugLastLeftHandIKSocketValid : 1 = false;

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
	FVector ViewModelPitchIKOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelPitchIKOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FVector ViewModelLeftUpperArmPitchLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|PitchOffset")
	FRotator ViewModelLeftUpperArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Aim")
	float ReloadAimAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Reload")
	float ViewModelReloadPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Fire")
	float ViewModelFireIKAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Equip")
	float ViewModelEquipPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|Slide")
	float ViewModelSlidePoseAlpha = 0.0f;

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
	FVector ViewModelWallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|FP Procedural|WallOffset")
	FRotator ViewModelWallOffsetRot = FRotator::ZeroRotator;

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
	int32 StartStopDirection = 0;
	uint8 bWasShouldMove : 1 = false;
	uint8 bWasSprinting : 1 = false;
	uint8 bHadWeaponPose : 1 = false;
};
