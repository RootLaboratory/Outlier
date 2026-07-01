#pragma once

#include "CoreMinimal.h"
#include "FirstPersonProceduralAnimRuntime.generated.h"

class UAnimSequenceBase;

USTRUCT(BlueprintType)
struct OUTLIER_API FFirstPersonProceduralAnimRuntime
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hip")
	FVector HipPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hip")
	FRotator HipPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "Idle")
	float IdleIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	FVector AimPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	FRotator AimPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float ReloadAimAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float ReloadPoseAlpha = 0.0f;

	// 재장전 시 팔을 미는 오프셋(이미 reload 알파로 스케일됨). 그래프에서 root 본에 적용.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	FVector ReloadPushLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	FRotator ReloadPushRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand")
	FVector LeftHandReloadGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand")
	FRotator LeftHandReloadGripOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand")
	FVector LeftUpperArmReloadLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand")
	FRotator LeftUpperArmReloadRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand")
	FRotator LeftLowerArmReloadRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand IK")
	FVector LeftHandReloadIKLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand IK")
	FRotator LeftHandReloadIKRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand IK")
	FVector LeftHandReloadJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload|Left Hand IK")
	float LeftHandReloadIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equip")
	float EquipPoseAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	float SlidePoseAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	float WeaponPoseAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Root")
	FVector WeaponRootLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Root")
	FRotator WeaponRootRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	FVector SprintPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	FRotator SprintPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float SprintAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	FRotator LeanRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	float  LeanAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FVector LeftHandIKLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FRotator LeftHandIKRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	float LeftHandIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	float LeftHandFreeAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FVector LeftHandJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FVector LeftUpperArmPitchLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FRotator LeftUpperArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	float LeftUpperArmPitchAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FRotator LeftLowerArmPitchRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	float LeftLowerArmPitchAlpha = 0.0f;

	// Route 1: ik_hand_gun(총손) 로컬 기준 왼손 그립 오프셋(정적).
	// 그래프에서 ik_hand_l = CopyBone(ik_hand_gun) + 이 오프셋(Bone Space)으로 두면
	// 예측 없이 모든 절차적 움직임에 손이 정확히 붙는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FVector LeftHandGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand")
	FRotator LeftHandGripOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Right Hand")
	FVector RightHandIKLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Right Hand")
	FRotator RightHandIKRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FVector MovementLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FRotator MovementRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float FingerMovementAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	FVector ForwardWalkLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	FRotator ForwardWalkRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	uint8 bIsForwardWalk : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	float ForwardWalkAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	FVector ForwardWalkAnimLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	FRotator ForwardWalkAnimRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Right Walk")
	float StrafeWalkAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Right Walk")
	FRotator CurrentStrafeWalkRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lower")
	FVector LowerPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lower")
	FRotator LowerPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lower")
	float LowerAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FVector RecoilLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	FRotator RecoilRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	FVector SwayLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	FRotator SwayRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	FVector JumpLandLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	FRotator JumpLandRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	float JumpLandAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	FVector WallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	FRotator WallOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallOffsetAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallMuzzleBlockAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	TObjectPtr<UAnimSequenceBase> WallAvoidUpPose = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	TObjectPtr<UAnimSequenceBase> WallAvoidDownPose = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallAvoidUpAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallAvoidDownAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallAvoidSideAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Offset")
	float WallAvoidSideSign = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start Stop")
	FVector StartStopLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Start Stop")
	FRotator StartStopRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsAiming : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	uint8 bIsReloading : 1 = false;
};
