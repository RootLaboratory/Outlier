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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Change")
	FVector  AnimMidpointOffsetIn = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Change")
	FVector  AnimMidpointOffsetOut = FVector::ZeroVector;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Blend", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float WeaponDetailAlphaEaseStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	float ReloadBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Reload")
	float ReloadBlendOutSpeed = 8.0f;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Follow")
	FVector LeftHandSprintJointTargetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FVector LeftHandSprintIKLocOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint")
	FRotator LeftHandSprintIKRotOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint|Pitch Curve")
	TObjectPtr<UCurveVector> LeftHandSprintPitchSocketLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|Sprint|Pitch Curve")
	TObjectPtr<UCurveVector> LeftHandSprintPitchSocketRotCurve = nullptr;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandEquipIKAlphaScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandSlideIKAlphaScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeftHandFireIKAlphaScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeftHand|IK Alpha", meta = (ClampMin = "0.0"))
	float LeftHandFireIKAlphaDecaySpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Idle")
	float FingerMovementAlpha = 0.0f;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchIKOffsetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchIKOffsetRotCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftHandJointTargetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftUpperArmLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|LeftHand Curve")
	TObjectPtr<UCurveVector> PitchLeftUpperArmRotCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Crouch LeftHand Curve")
	TObjectPtr<UCurveVector> CrouchPitchIKOffsetLocCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|Pitch Offset|Crouch LeftHand Curve")
	TObjectPtr<UCurveVector> CrouchPitchIKOffsetRotCurve = nullptr;

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
	FVector WallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViewModel|WallOffset")
	FRotator WallOffsetRot = FRotator::ZeroRotator;

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
