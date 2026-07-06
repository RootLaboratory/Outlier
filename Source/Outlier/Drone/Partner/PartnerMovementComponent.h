// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerMovementComponent.generated.h"

enum class EPartnerMoveMode : uint8;

struct FPartnerMovementKinematics
{
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	FVector LocalVelocity = FVector::ZeroVector;
	FVector LocalAcceleration = FVector::ZeroVector;
	float MassFactor = 1.0f;
	float SpeedAlpha = 0.0f;
};

struct FPartnerTiltTarget
{
	float Pitch = 0.0f;
	float Roll = 0.0f;
	float EffectiveMaxPitch = 0.0f;
	float EffectiveMaxRoll = 0.0f;
	float TurnRoll = 0.0f;
	float TiltStrength = 1.0f;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class OUTLIER_API UPartnerMovementComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:	
	UPartnerMovementComponent();

	void RefreshMovementState();
	void ApplyCameraAssist();
	void StopCameraAssist();
	void SetSyncMove(bool SyncMove);
	void SetFreeMove(bool FreeMove);
	void SetMoveInput(const FVector2D& MoveInput);
	void SetVerticalInput(const float Axis);
	void OnMoveModeChanged(EPartnerMoveMode NewMode);
	void ResetMovementFeel();

	void RefreshTickEnabled();

	float GetCurrentCameraPitchDegrees() const { return CurrentCameraPitch; }
	float GetCurrentCameraRollDegrees() const { return CurrentCameraRoll; }

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DirectionWeight = 500.0f;

private:
	uint8 bMovementFeelInitialized : 1 = false;
	uint8 bVisualTiltInitialized : 1 = false;
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	float VerticalInput     = 0.0f;

	float CurrentInertialPitch = 0.0f;
	float CurrentInertialRoll = 0.0f;
	float CurrentCameraPitch = 0.0f;
	float CurrentCameraRoll = 0.0f;
	FVector2D SmoothedTiltTarget = FVector2D::ZeroVector;

	bool bPitchReboundActive = false;
	bool bRollReboundActive = false;

	float PitchReboundTarget = 0.0f;
	float RollReboundTarget = 0.0f;

	float PreviousTargetPitch = 0.0f;
	float PreviousTargetRoll = 0.0f;

	FRotator BaseMeshRelativeRotation = FRotator::ZeroRotator;
	FRotator BaseViewModelRelativeRotation = FRotator::ZeroRotator;
	FVector LastLocation = FVector::ZeroVector;
	FVector SmoothedVelocity = FVector::ZeroVector;

private:
	void ApplyManualMoveInput();

	void UpdateNormalMove();
	void UpdateFreeMove();
	void UpdateSyncMove(float DeltaTime);
	void UpdateCameraAssist(float DeltaTime);

	void UpdateMovementFeel(float DeltaTime);
	bool CanUpdateMovementFeel(float DeltaTime) const;
	FPartnerMovementKinematics CalculateMovementKinematics(float DeltaTime);
	FPartnerTiltTarget CalculateTiltTargets(const FPartnerMovementKinematics& Kinematics) const;
	void UpdateInertialTilt(
		const FPartnerTiltTarget& TiltTarget,
		const FPartnerMovementKinematics& Kinematics,
		float DeltaTime
	);
	void ApplyCameraTilt();
	void ApplyVisualTilt(float DeltaTime);
	void ApplyMeshTilt(float DeltaTime);
	void ApplyViewModelTilt(float DeltaTime);

	void EnterSyncMove();

	void MoveTowardTargetWithAvoidance(
		const FVector& TargetLocation,
		float DeltaTime,
		float InterpSpeed
	);

	FVector FindSimpleAvoidanceTarget(
		const FVector& CurrentLocation,
		const FVector& TargetLocation,
		const FHitResult& Hit
	);

	bool IsPathClear(const FVector& From, const FVector& To) const;

	float GetMovementMassFactor() const;
	float ResolveInertialReboundAxis(
		float CurrentValue,
		float DesiredTarget,
		float MaxAbsValue,
		float SpeedAlpha,
		float MassFactor,
		float& PreviousDesiredTarget,
		bool& bReboundActive,
		float& ReboundTarget,
		float& OutInterpSpeedMultiplier
	) const;

	void UpdateInputMovement();
	void UpdateServerDrivenMovement(float DeltaTime);
};
