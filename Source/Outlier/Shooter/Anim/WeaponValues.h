#pragma once

#include "CoreMinimal.h"
#include "WeaponValues.generated.h"

class UAnimSequenceBase;
class UCurveVector;

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementStrengthRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	FVector  AimMidpointOffsetIn = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	FVector  AimMidpointOffsetOut = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimInterpSpeedIn = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0"))
	float AimInterpSpeedOut = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimPitchOffsetScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimSwayScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimMovementProceduralScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Sprint Transition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintToAimGateStartSprintAlpha = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Sprint Transition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintToAimGateEndSprintAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint|Aim Transition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimToSprintGateStartAimAlpha = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint|Aim Transition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimToSprintGateEndAimAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Hip")
	FVector HipPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Hip")
	FRotator HipPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Aim")
	FVector AimPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Aim")
	FRotator AimPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	FVector  SprintPoseLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	FRotator SprintPoseRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float SprintInterpSpeedIn = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float SprintInterpSpeedOut = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SprintAlphaEaseStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float NonSprintProceduralBlendInSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float NonSprintProceduralBlendOutSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float SprintAnimMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float LeftHandSprintAnimMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Blend", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float WeaponDetailAlphaEaseStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	float ReloadBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	float ReloadBlendOutSpeed = 8.0f;

	// 재장전 시 팔 전체를 미는 오프셋(컴포넌트 스페이스: X=앞, Y=오른쪽, Z=위). 카메라 클리핑(손이 카메라 파고듦) 방지용.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	FVector ReloadPushLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	FRotator ReloadPushRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand")
	FVector LeftHandReloadGripOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand")
	FRotator LeftHandReloadGripOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand")
	FVector LeftUpperArmReloadLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand")
	FRotator LeftUpperArmReloadRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand")
	FRotator LeftLowerArmReloadRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand IK")
	FVector LeftHandReloadJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand IK")
	FVector LeftHandReloadIKLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand IK")
	FRotator LeftHandReloadIKRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandReloadIKAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|LeftHand IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandReloadArmAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|RightHand")
	FVector RightHandReloadIKLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload|RightHand")
	FRotator RightHandReloadIKRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Slide")
	float SlideBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Slide")
	float SlideBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand")
	FVector LeftHandRecoilLocScale = FVector(0.08f, 0.12f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand")
	float LeftHandRecoilRotScale = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Recoil")
	FVector LeftHandRecoilJointTargetLocScale = FVector(0.08f, 0.12f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Recoil")
	FVector LeftUpperArmRecoilLocScale = FVector(0.02f, 0.04f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Recoil")
	float LeftUpperArmRecoilRotScale = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	FVector LeftHandSprintLocScale = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	float LeftHandSprintRotScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket")
	FVector LeftHandGripSocketLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket")
	FRotator LeftHandGripSocketRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset")
	FVector LeftHandWallMuzzleBlockSocketLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset")
	FRotator LeftHandWallMuzzleBlockSocketRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset", meta = (ClampMin = "0.0"))
	float LeftHandWallMuzzleBlockSocketOffsetAlphaScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset|VeryClose")
	FVector LeftHandWallVeryCloseSocketLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset|VeryClose")
	FRotator LeftHandWallVeryCloseSocketRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Grip Socket|WallOffset|VeryClose", meta = (ClampMin = "0.0"))
	float LeftHandWallVeryCloseSocketOffsetAlphaScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	FVector LeftHandSprintJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FVector LeftHandSprintIKLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FRotator LeftHandSprintIKRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint|Pitch Curve")
	TObjectPtr<UCurveVector> LeftHandSprintPitchJointTargetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint|Pitch Curve")
	TObjectPtr<UCurveVector> LeftHandSprintPitchUpperArmLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint|Pitch Curve")
	TObjectPtr<UCurveVector> LeftHandSprintPitchUpperArmRotCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FVector LeftUpperArmSprintLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FRotator LeftUpperArmSprintRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	FVector LeftHandWalkLocScale = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	float LeftHandWalkRotScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	FVector LeftHandJumpLocScale = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	float LeftHandJumpRotScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandReloadIKAlphaScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandReloadIKBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandReloadIKBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandEquipIKAlphaScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandEquipIKBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandEquipIKBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandSlideIKAlphaScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandSlideIKBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandSlideIKBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandFireIKAlphaScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandFireIKAlphaDecaySpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle")
	float FingerMovementAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleHipIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleAimIntensity = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0"))
	float IdleIntensityInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0"))
	float FingerMovementPulseMinTime = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0"))
	float FingerMovementPulseMaxTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0"))
	float FingerMovementIntervalMinTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle", meta = (ClampMin = "0.0"))
	float FingerMovementIntervalMaxTime = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Lean")
	float FirstPersonLeanYawDegrees = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Lean", meta = (ClampMin = "0.0"))
	float FirstPersonLeanInterpSpeed = 12.0f;

	// Walk
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	FVector WalkTiltLoc = FVector(0.0f, 0.0f, -0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	FRotator WalkTiltRot = FRotator(0.0f, -2.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	FVector WalkAnimLocAmplitude = FVector(0.15f, 0.25f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	FRotator WalkAnimRotAmplitude = FRotator(0.5f, 0.8f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	float WalkAnimRotPhaseOffset = 0.35f;			// 회전이 위치보다 늦게 따라오는 지연(라디안). 0이면 동기, 0.2~0.5 권장

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Walk")
	FRotator StrafeWalkRot = FRotator(0.0f, 0.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Sway")
	FVector SwayLocAmplitude = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Sway")
	FRotator SwayRotAmplitude = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Sway")
	float SwayMaxLookSpeedDegreesPerSecond = 720.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Curve")
	float PitchOffsetMin = -60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Curve")
	float PitchOffsetMax = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LowerArm")
	FRotator LeftLowerArmRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftHandJointTargetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftUpperArmLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftUpperArmRotCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Crouch LeftHand Curve")
	TObjectPtr<UCurveVector> CrouchPitchLeftHandJointTargetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Crouch LeftHand Curve")
	TObjectPtr<UCurveVector> CrouchPitchLeftUpperArmLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Crouch LeftHand Curve")
	TObjectPtr<UCurveVector> CrouchPitchLeftUpperArmRotCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Jump")
	FVector JumpLandLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Jump")
	FRotator JumpLandRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallTraceDistance = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallTraceRadius = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallSafeDistance = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallProbeForwardOffset = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallProbeRightOffset = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallProbeUpOffset = -8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallVerticalProbeOffset = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallSideProbeOffset = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Pose")
	TObjectPtr<UAnimSequenceBase> WallAvoidUpPose = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Pose")
	TObjectPtr<UAnimSequenceBase> WallAvoidDownPose = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Pose", meta = (ClampMin = "0.0"))
	float WallAvoidUpPoseAlphaScale = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Pose", meta = (ClampMin = "0.0"))
	float WallMuzzleBlockUpPoseScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Pose", meta = (ClampMin = "0.0"))
	float WallMuzzleBlockDownPoseScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	FVector WallMaxOffsetLoc = FVector(0.0f, -4.0f, -1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockTraceDistance = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockTraceRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockSafeDistance = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockForwardOffset = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockRightOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockUpOffset = -8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallBarrelBlockLength = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallBarrelBlockTraceRadius = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallBarrelBlockSafeDistance = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallTopEdgeBarrelBlockScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallTopEdgeVeryCloseScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallTopEdgeBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallTopEdgeBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Ceiling", meta = (ClampMin = "0.0"))
	float WallCeilingBlendInSpeed = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Ceiling", meta = (ClampMin = "0.0"))
	float WallCeilingBlendOutSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallPitchBlockTraceRadiusBoost = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallPitchBlockLengthBoost = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	FVector WallMuzzleBlockLoc = FVector(0.0f, -14.0f, -2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	FRotator WallMuzzleBlockRot = FRotator(-25.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	FRotator WallMuzzleBlockDownRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockBlendInSpeed = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock")
	float WallMuzzleBlockBlendOutSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallMuzzleBlockReleaseHoldTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float WallMuzzleBlockPreferenceBlendSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopClearance = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopRange = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop")
	FVector WallHardStopLoc = FVector(0.0f, -16.0f, -5.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop")
	FRotator WallHardStopRot = FRotator(-8.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopBlendInSpeed = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopBlendOutSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopTargetSmoothSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopSafetyBlendInSpeed = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|HardStop", meta = (ClampMin = "0.0"))
	float WallHardStopSafetyBlendOutSpeed = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryCloseStartAlpha = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryCloseFullAlpha = 0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose")
	FVector WallVeryCloseLoc = FVector(0.0f, -6.0f, -4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose")
	FRotator WallVeryCloseRot = FRotator(-8.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryCloseMuzzleBlockScale = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryCloseMuzzleRotSuppressScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryCloseMuzzleOffsetSuppressScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallVeryClosePoseSuppressScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose")
	float WallVeryCloseBlendInSpeed = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|VeryClose")
	float WallVeryCloseBlendOutSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	FRotator WallMaxOffsetRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	FRotator WallSideOffsetRot = FRotator(0.0f, 10.0f, 12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand")
	FVector LeftHandWallSideIKLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand", meta = (DeprecatedProperty, DeprecationMessage = "Use Wall Muzzle Block arm offsets instead. MuzzleBlock should not move ik_hand_l directly."))
	FVector LeftHandWallMuzzleBlockIKLocOffset = FVector(0.0f, 2.0f, -2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand", meta = (DeprecatedProperty, DeprecationMessage = "Use Wall Muzzle Block arm offsets instead. MuzzleBlock should not rotate ik_hand_l directly."))
	FRotator LeftHandWallMuzzleBlockIKRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Use LeftHandWallMuzzleBlockArmAlphaScale instead."))
	float LeftHandWallMuzzleBlockIKLocAlphaScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand", meta = (ClampMin = "0.0"))
	float LeftHandWallMuzzleBlockArmAlphaScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand")
	FVector LeftHandWallMuzzleBlockJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand")
	FVector LeftUpperArmWallMuzzleBlockLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand")
	FRotator LeftUpperArmWallMuzzleBlockRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|LeftHand")
	FRotator LeftLowerArmWallMuzzleBlockRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallSideCloseOffsetScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallBlendInSpeed = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	float WallBlendOutSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Debug")
	uint8 bDrawWallOffsetDebug : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset|Debug", meta = (EditCondition = "bDrawWallOffsetDebug", ClampMin = "0.0"))
	float WallOffsetDebugDrawTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|StartStop")
	FVector StartMoveLoc = FVector(-0.4f, 0.0f, -0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|StartStop")
	FRotator StartMoveRot = FRotator(0.0f, 1.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|StartStop")
	FVector StopMoveLoc = FVector(0.4f, 0.0f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|StartStop")
	FRotator StopMoveRot = FRotator(0.0f, -1.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|StartStop")
	float StartStopDuration = 0.15f;
};
