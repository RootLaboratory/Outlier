#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlightMovementComponentBase.generated.h"

class ACharacter;
class USceneComponent;
class USkeletalMeshComponent;

enum class EFlightInputMode : uint8
{
	Horizontal,
	Free
};

struct FFlightMovementKinematics
{
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	FVector LocalVelocity = FVector::ZeroVector;
	FVector LocalAcceleration = FVector::ZeroVector;
	float MassFactor = 1.0f;
	float SpeedAlpha = 0.0f;
};

struct FFlightTiltTarget
{
	float Pitch = 0.0f;
	float Roll = 0.0f;
	float EffectiveMaxPitch = 0.0f;
	float EffectiveMaxRoll = 0.0f;
	float TurnRoll = 0.0f;
	float TiltStrength = 1.0f;
};

UCLASS(Abstract, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UFlightMovementComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightMovementComponentBase();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

	virtual void SetMoveInput(const FVector2D& MoveInput);
	virtual void SetVerticalInput(float Axis);
	void ClearFlightInput();
	void ResetMovementFeel();
	void RefreshTickEnabled();
	void ApplyExternalImpactTilt(
		const FVector& WorldDirection,
		float NormalizedStrength,
		float MaxTiltDegrees,
		float RecoveryInterpSpeed);

	float GetCurrentCameraPitchDegrees() const { return CurrentCameraPitch; }
	float GetCurrentCameraRollDegrees() const { return CurrentCameraRoll; }
	float GetCameraPitchOnMove() const { return CameraPitchOnMove; }
	float GetCameraRollOnTurn() const { return CameraRollOnTurn; }

	void SetMoveSpeed(float NewMoveSpeed);
	void SetBoostSpeed(float NewBoostSpeed);
	void SetVerticalSpeed(float NewVerticalSpeed);
	void SetAcceleration(float NewAcceleration);
	void SetDeceleration(float NewDeceleration);
	void SetRotationLagAmount(float NewRotationLagAmount);
	void SetRotationLagRecoverSpeed(float NewRotationLagRecoverSpeed);
	void SetCameraPitchOnMove(float NewCameraPitchOnMove);
	void SetCameraRollOnTurn(float NewCameraRollOnTurn);
	void SetCameraRollInterpSpeed(float NewCameraRollInterpSpeed);
	void SetMeshInertialTiltScale(float NewMeshInertialTiltScale);
	void SetCameraInertialTiltScale(float NewCameraInertialTiltScale);
	void SetViewModelInertialTiltScale(float NewViewModelInertialTiltScale);
	void SetInertialTiltReboundRatio(float NewInertialTiltReboundRatio);
	void SetInertialTiltReboundMaxScale(float NewInertialTiltReboundMaxScale);
	void SetInertialTiltReboundInterpMultiplier(float NewInertialTiltReboundInterpMultiplier);
	void SetInertialTiltRecoverInterpMultiplier(float NewInertialTiltRecoverInterpMultiplier);
	void SetAccelerating(bool bNewAccelerating);

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;

	virtual ACharacter* GetFlightOwnerCharacter() const;
	virtual USkeletalMeshComponent* GetFlightVisualMesh() const;
	virtual USceneComponent* GetFlightVisualTiltRoot() const;
	virtual USceneComponent* GetFlightViewModelRoot() const;
	virtual bool CanRunInputMovement() const;
	virtual bool ShouldUpdateMovementFeel() const;
	virtual EFlightInputMode GetFlightInputMode() const;
	virtual void OnAfterInputMovement(float DeltaTime);

	bool HasMoveInput() const;
	FVector BuildWorldMoveVector(float VerticalInputScale) const;
	void UpdateInputMovement();
	void UpdateMovementFeel(float DeltaTime);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Move")
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Move")
	float BoostSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Move")
	float VerticalSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Move")
	float Acceleration = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Move")
	float Deceleration = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float RotationLagAmount = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float RotationLagRecoverSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float CameraPitchOnMove = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float CameraRollOnTurn = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float CameraRollInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float MeshInertialTiltScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float MeshPitchInertialTiltScale = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float MeshRollInertialTiltScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float CameraInertialTiltScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float ViewModelInertialTiltScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float InertialTiltReboundRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float InertialTiltReboundMaxScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float InertialTiltReboundInterpMultiplier = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight|Feel")
	float InertialTiltRecoverInterpMultiplier = 1.8f;

	uint8 bIsAccelerating : 1 = false;
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	float VerticalInput = 0.0f;

private:
	bool CanUpdateMovementFeel(float DeltaTime) const;
	FFlightMovementKinematics CalculateMovementKinematics(float DeltaTime);
	FFlightTiltTarget CalculateTiltTargets(const FFlightMovementKinematics& Kinematics) const;
	void UpdateInertialTilt(
		const FFlightTiltTarget& TiltTarget,
		const FFlightMovementKinematics& Kinematics,
		float DeltaTime
	);
	void UpdateMeshInertialTilt(const FFlightMovementKinematics& Kinematics, float DeltaTime);
	void ApplyCameraTilt();
	void ApplyVisualTilt(float DeltaTime);
	void ApplyMeshTilt();
	void ApplyViewModelTilt(float DeltaTime);
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

	uint8 bMovementFeelInitialized : 1 = false;
	uint8 bVisualTiltInitialized : 1 = false;
	uint8 bViewModelTiltInitialized : 1 = false;
	float CurrentInertialPitch = 0.0f;
	float CurrentInertialRoll = 0.0f;
	float CurrentMeshPitch = 0.0f;
	float CurrentMeshRoll = 0.0f;
	float CurrentCameraPitch = 0.0f;
	float CurrentCameraRoll = 0.0f;
	FVector2D SmoothedTiltTarget = FVector2D::ZeroVector;
	bool bPitchReboundActive = false;
	bool bRollReboundActive = false;
	float PitchReboundTarget = 0.0f;
	float RollReboundTarget = 0.0f;
	float PreviousTargetPitch = 0.0f;
	float PreviousTargetRoll = 0.0f;
	bool bMeshPitchReboundActive = false;
	bool bMeshRollReboundActive = false;
	float MeshPitchReboundTarget = 0.0f;
	float MeshRollReboundTarget = 0.0f;
	float PreviousMeshTargetPitch = 0.0f;
	float PreviousMeshTargetRoll = 0.0f;
	FRotator BaseMeshRelativeRotation = FRotator::ZeroRotator;
	FRotator BaseViewModelRelativeRotation = FRotator::ZeroRotator;
	float PreviousActorYaw = 0.0f;
	bool bMeshRotationInitialized = false;
	FVector LastLocation = FVector::ZeroVector;
	FVector SmoothedVelocity = FVector::ZeroVector;
	float CurrentImpactMeshPitch = 0.0f;
	float CurrentImpactMeshRoll = 0.0f;
	float ImpactMeshTiltInterpSpeed = 8.0f;
	uint8 bExternalImpactTiltActive : 1 = false;
};
