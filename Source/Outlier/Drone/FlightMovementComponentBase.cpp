#include "Drone/FlightMovementComponentBase.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UFlightMovementComponentBase::UFlightMovementComponentBase()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UFlightMovementComponentBase::OnRegister()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	Super::OnRegister();

	Activate(true);
	SetComponentTickEnabled(true);
}

void UFlightMovementComponentBase::BeginPlay()
{
	Super::BeginPlay();

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	Activate(true);
	SetComponentTickEnabled(true);

	if (USkeletalMeshComponent* VisualMesh = GetFlightVisualMesh())
	{
		BaseMeshRelativeRotation = VisualMesh->GetRelativeRotation();
		bVisualTiltInitialized = true;
	}

	if (USceneComponent* ViewModelRoot = GetFlightViewModelRoot())
	{
		BaseViewModelRelativeRotation = ViewModelRoot->GetRelativeRotation();
		bViewModelTiltInitialized = true;
	}

	RefreshTickEnabled();
}

void UFlightMovementComponentBase::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetFlightOwnerCharacter())
	{
		return;
	}

	if (CanRunInputMovement())
	{
		UpdateInputMovement();
	}

	OnAfterInputMovement(DeltaTime);

	if (ShouldUpdateMovementFeel())
	{
		UpdateMovementFeel(DeltaTime);
	}

	RefreshTickEnabled();
}

void UFlightMovementComponentBase::SetMoveInput(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::SetVerticalInput(float Axis)
{
	VerticalInput = Axis;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::ResetMovementFeel()
{
	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();

	bMovementFeelInitialized = false;
	SmoothedVelocity = OwnerCharacter
		? OwnerCharacter->GetVelocity()
		: FVector::ZeroVector;
	SmoothedTiltTarget = FVector2D::ZeroVector;
	CurrentInertialPitch = 0.0f;
	CurrentInertialRoll = 0.0f;
	CurrentCameraPitch = 0.0f;
	CurrentCameraRoll = 0.0f;
	PreviousTargetPitch = 0.0f;
	PreviousTargetRoll = 0.0f;
	bPitchReboundActive = false;
	bRollReboundActive = false;
	PitchReboundTarget = 0.0f;
	RollReboundTarget = 0.0f;
}

void UFlightMovementComponentBase::RefreshTickEnabled()
{
	const bool bShouldTick = GetFlightOwnerCharacter() != nullptr;
	SetComponentTickEnabled(bShouldTick);
}

void UFlightMovementComponentBase::SetMoveSpeed(float NewMoveSpeed)
{
	MoveSpeed = FMath::Max(NewMoveSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetBoostSpeed(float NewBoostSpeed)
{
	BoostSpeed = FMath::Max(NewBoostSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetVerticalSpeed(float NewVerticalSpeed)
{
	VerticalSpeed = FMath::Max(NewVerticalSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetAcceleration(float NewAcceleration)
{
	Acceleration = FMath::Max(NewAcceleration, 0.0f);
}

void UFlightMovementComponentBase::SetDeceleration(float NewDeceleration)
{
	Deceleration = FMath::Max(NewDeceleration, 0.0f);
}

void UFlightMovementComponentBase::SetRotationLagAmount(float NewRotationLagAmount)
{
	RotationLagAmount = FMath::Max(NewRotationLagAmount, 0.0f);
}

void UFlightMovementComponentBase::SetRotationLagRecoverSpeed(float NewRotationLagRecoverSpeed)
{
	RotationLagRecoverSpeed = FMath::Max(NewRotationLagRecoverSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetCameraPitchOnMove(float NewCameraPitchOnMove)
{
	CameraPitchOnMove = FMath::Max(NewCameraPitchOnMove, 0.0f);
}

void UFlightMovementComponentBase::SetCameraRollOnTurn(float NewCameraRollOnTurn)
{
	CameraRollOnTurn = FMath::Max(NewCameraRollOnTurn, 0.0f);
}

void UFlightMovementComponentBase::SetCameraRollInterpSpeed(float NewCameraRollInterpSpeed)
{
	CameraRollInterpSpeed = FMath::Max(NewCameraRollInterpSpeed, 0.0f);
}

void UFlightMovementComponentBase::SetMeshInertialTiltScale(float NewMeshInertialTiltScale)
{
	MeshInertialTiltScale = NewMeshInertialTiltScale;
}

void UFlightMovementComponentBase::SetCameraInertialTiltScale(float NewCameraInertialTiltScale)
{
	CameraInertialTiltScale = NewCameraInertialTiltScale;
}

void UFlightMovementComponentBase::SetViewModelInertialTiltScale(float NewViewModelInertialTiltScale)
{
	ViewModelInertialTiltScale = NewViewModelInertialTiltScale;
}

void UFlightMovementComponentBase::SetInertialTiltReboundRatio(float NewInertialTiltReboundRatio)
{
	InertialTiltReboundRatio = FMath::Clamp(NewInertialTiltReboundRatio, 0.0f, 1.0f);
}

void UFlightMovementComponentBase::SetInertialTiltReboundMaxScale(float NewInertialTiltReboundMaxScale)
{
	InertialTiltReboundMaxScale = FMath::Max(NewInertialTiltReboundMaxScale, 0.0f);
}

void UFlightMovementComponentBase::SetInertialTiltReboundInterpMultiplier(float NewInertialTiltReboundInterpMultiplier)
{
	InertialTiltReboundInterpMultiplier = FMath::Max(NewInertialTiltReboundInterpMultiplier, 1.0f);
}

void UFlightMovementComponentBase::SetInertialTiltRecoverInterpMultiplier(float NewInertialTiltRecoverInterpMultiplier)
{
	InertialTiltRecoverInterpMultiplier = FMath::Max(NewInertialTiltRecoverInterpMultiplier, 1.0f);
}

void UFlightMovementComponentBase::SetAccelerating(bool bNewAccelerating)
{
	bIsAccelerating = bNewAccelerating;
}

ACharacter* UFlightMovementComponentBase::GetFlightOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

USkeletalMeshComponent* UFlightMovementComponentBase::GetFlightVisualMesh() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

USceneComponent* UFlightMovementComponentBase::GetFlightViewModelRoot() const
{
	return nullptr;
}

bool UFlightMovementComponentBase::CanRunInputMovement() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter && (OwnerCharacter->HasAuthority() || OwnerCharacter->IsLocallyControlled());
}

bool UFlightMovementComponentBase::ShouldUpdateMovementFeel() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	return OwnerCharacter && OwnerCharacter->IsLocallyControlled();
}

EFlightInputMode UFlightMovementComponentBase::GetFlightInputMode() const
{
	return EFlightInputMode::Free;
}

void UFlightMovementComponentBase::OnAfterInputMovement(float DeltaTime)
{
	(void)DeltaTime;
}

bool UFlightMovementComponentBase::HasMoveInput() const
{
	return !CurrentMoveInput.IsNearlyZero(0.01f) ||
		!FMath::IsNearlyZero(VerticalInput, 0.01f);
}

void UFlightMovementComponentBase::ClearFlightInput()
{
	CurrentMoveInput = FVector2D::ZeroVector;
	VerticalInput = 0.0f;
	RefreshTickEnabled();
}

void UFlightMovementComponentBase::UpdateInputMovement()
{
	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	const float CurrentSpeed = bIsAccelerating ? BoostSpeed : MoveSpeed;
	const float VerticalScale = VerticalSpeed / FMath::Max(CurrentSpeed, KINDA_SMALL_NUMBER);

	FVector Move = FVector::ZeroVector;
	if (GetFlightInputMode() == EFlightInputMode::Horizontal)
	{
		Move =
			OwnerCharacter->GetActorForwardVector() * CurrentMoveInput.Y +
			OwnerCharacter->GetActorRightVector() * CurrentMoveInput.X;
		Move.Z = 0.0f;
		Move += FVector::UpVector * VerticalInput * VerticalScale;
	}
	else
	{
		FRotator ViewRot = OwnerCharacter->GetControlRotation();
		ViewRot.Roll = 0.0f;
		Move =
			ViewRot.Vector() * CurrentMoveInput.Y +
			FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y) * CurrentMoveInput.X +
			FVector::UpVector * VerticalInput * VerticalScale;
	}

	if (!Move.IsNearlyZero())
	{
		OwnerCharacter->AddMovementInput(Move.GetClampedToMaxSize(1.0f), 1.0f);
	}
}

void UFlightMovementComponentBase::UpdateMovementFeel(float DeltaTime)
{
	if (!CanUpdateMovementFeel(DeltaTime))
	{
		return;
	}

	if (!bVisualTiltInitialized)
	{
		if (USkeletalMeshComponent* VisualMesh = GetFlightVisualMesh())
		{
			BaseMeshRelativeRotation = VisualMesh->GetRelativeRotation();
			bVisualTiltInitialized = true;
		}
	}

	if (!bViewModelTiltInitialized)
	{
		if (USceneComponent* ViewModelRoot = GetFlightViewModelRoot())
		{
			BaseViewModelRelativeRotation = ViewModelRoot->GetRelativeRotation();
			bViewModelTiltInitialized = true;
		}
	}

	const FFlightMovementKinematics Kinematics = CalculateMovementKinematics(DeltaTime);
	const FFlightTiltTarget DesiredTarget = CalculateTiltTargets(Kinematics);

	UpdateInertialTilt(DesiredTarget, Kinematics, DeltaTime);
	ApplyCameraTilt();
	ApplyVisualTilt(DeltaTime);

	if (const ACharacter* OwnerCharacter = GetFlightOwnerCharacter())
	{
		LastLocation = OwnerCharacter->GetActorLocation();
	}
}

bool UFlightMovementComponentBase::CanUpdateMovementFeel(float DeltaTime) const
{
	return GetFlightOwnerCharacter() && DeltaTime > KINDA_SMALL_NUMBER;
}

FFlightMovementKinematics UFlightMovementComponentBase::CalculateMovementKinematics(float DeltaTime)
{
	FFlightMovementKinematics Result;

	ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	if (!OwnerCharacter)
	{
		return Result;
	}

	const FVector CurrentLocation = OwnerCharacter->GetActorLocation();
	if (!bMovementFeelInitialized)
	{
		LastLocation = CurrentLocation;
		bMovementFeelInitialized = true;
	}

	const FVector LocationVelocity = (CurrentLocation - LastLocation) / DeltaTime;
	FVector RawVelocity = OwnerCharacter->GetVelocity();
	if (RawVelocity.IsNearlyZero(1.0f) && !LocationVelocity.IsNearlyZero(1.0f))
	{
		RawVelocity = LocationVelocity;
	}

	Result.Acceleration = (RawVelocity - SmoothedVelocity) / DeltaTime;
	Result.Acceleration = Result.Acceleration.GetClampedToMaxSize(FMath::Max(Acceleration * 2.0f, 1.0f));

	SmoothedVelocity = FMath::VInterpTo(
		SmoothedVelocity,
		RawVelocity,
		DeltaTime,
		FMath::Max(RotationLagRecoverSpeed, 1.0f)
	);

	Result.Velocity = RawVelocity;
	Result.LocalVelocity = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(RawVelocity);
	Result.LocalAcceleration = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(Result.Acceleration);
	Result.MassFactor = GetMovementMassFactor();

	const float SpeedReference = FMath::Max(BoostSpeed, MoveSpeed);
	Result.SpeedAlpha = FMath::Clamp(
		Result.LocalVelocity.Size() / FMath::Max(SpeedReference, 1.0f),
		0.0f,
		1.0f
	);

	return Result;
}

FFlightTiltTarget UFlightMovementComponentBase::CalculateTiltTargets(const FFlightMovementKinematics& Kinematics) const
{
	FFlightTiltTarget Result;

	const float AccelReference = FMath::Max(Acceleration, 1.0f);
	Result.TiltStrength = FMath::Clamp(RotationLagAmount * 12.5f, 0.1f, 3.0f);
	Result.EffectiveMaxPitch = FMath::Max(CameraPitchOnMove, 0.0f) / Kinematics.MassFactor;
	Result.EffectiveMaxRoll = FMath::Max(CameraRollOnTurn, 0.0f) / Kinematics.MassFactor;

	const float PitchResponseScale = HasMoveInput() ? 1.0f : 0.5f;
	const float AccelPitch = FMath::Clamp(
		-Kinematics.LocalAcceleration.X / AccelReference * Result.EffectiveMaxPitch * 0.45f * PitchResponseScale,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);
	const float AccelRoll = FMath::Clamp(
		Kinematics.LocalAcceleration.Y / AccelReference * Result.EffectiveMaxRoll,
		-Result.EffectiveMaxRoll,
		Result.EffectiveMaxRoll
	);

	float TurnRoll = 0.0f;
	if (!Kinematics.LocalVelocity.IsNearlyZero(20.0f))
	{
		const float RollDirectionScale = CurrentMoveInput.Y < -0.2f ? -1.0f : 1.0f;
		const FVector LocalVelocityDir = Kinematics.LocalVelocity.GetSafeNormal();
		FVector DesiredLocalDirection(CurrentMoveInput.Y, CurrentMoveInput.X, VerticalInput * 0.35f);
		if (!DesiredLocalDirection.IsNearlyZero())
		{
			DesiredLocalDirection.Normalize();
			const float DirectionChange = FMath::Clamp(
				DesiredLocalDirection.Y - LocalVelocityDir.Y,
				-1.0f,
				1.0f
			);
			TurnRoll += DirectionChange * Result.EffectiveMaxRoll * 0.5f * RollDirectionScale;
		}

		const float SideSpeedAlpha = FMath::Clamp(
			Kinematics.LocalVelocity.Y / FMath::Max(MoveSpeed, 1.0f),
			-1.0f,
			1.0f
		);
		TurnRoll += SideSpeedAlpha * Result.EffectiveMaxRoll * 0.35f * RollDirectionScale;
	}

	const float VerticalPitch = FMath::Clamp(
		-Kinematics.LocalAcceleration.Z / AccelReference * Result.EffectiveMaxPitch * 0.15f * PitchResponseScale,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);

	Result.TurnRoll = TurnRoll;
	Result.Pitch = FMath::Clamp(
		(AccelPitch + VerticalPitch) * Result.TiltStrength,
		-Result.EffectiveMaxPitch,
		Result.EffectiveMaxPitch
	);
	Result.Roll = FMath::Clamp(
		(AccelRoll + TurnRoll) * Result.TiltStrength,
		-Result.EffectiveMaxRoll,
		Result.EffectiveMaxRoll
	);

	return Result;
}

void UFlightMovementComponentBase::UpdateInertialTilt(
	const FFlightTiltTarget& TiltTarget,
	const FFlightMovementKinematics& Kinematics,
	float DeltaTime)
{
	float PitchInterpSpeedMultiplier = 1.0f;
	float RollInterpSpeedMultiplier = 1.0f;
	const float TargetBlendSpeed = FMath::Max(CameraRollInterpSpeed, 1.0f);

	SmoothedTiltTarget.X = FMath::FInterpTo(SmoothedTiltTarget.X, TiltTarget.Pitch, DeltaTime, TargetBlendSpeed);
	SmoothedTiltTarget.Y = FMath::FInterpTo(SmoothedTiltTarget.Y, TiltTarget.Roll, DeltaTime, TargetBlendSpeed);

	FFlightTiltTarget ResolvedTarget = TiltTarget;
	ResolvedTarget.Pitch = ResolveInertialReboundAxis(
		CurrentInertialPitch,
		SmoothedTiltTarget.X,
		TiltTarget.EffectiveMaxPitch,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousTargetPitch,
		bPitchReboundActive,
		PitchReboundTarget,
		PitchInterpSpeedMultiplier
	);
	ResolvedTarget.Roll = ResolveInertialReboundAxis(
		CurrentInertialRoll,
		SmoothedTiltTarget.Y,
		TiltTarget.EffectiveMaxRoll,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousTargetRoll,
		bRollReboundActive,
		RollReboundTarget,
		RollInterpSpeedMultiplier
	);

	const float CoupledInterpSpeedMultiplier = FMath::Max(PitchInterpSpeedMultiplier, RollInterpSpeedMultiplier);
	const float FeelInterpSpeed = CameraRollInterpSpeed / Kinematics.MassFactor * CoupledInterpSpeedMultiplier;

	CurrentInertialPitch = FMath::FInterpTo(CurrentInertialPitch, ResolvedTarget.Pitch, DeltaTime, FeelInterpSpeed);
	CurrentInertialRoll = FMath::FInterpTo(CurrentInertialRoll, ResolvedTarget.Roll, DeltaTime, FeelInterpSpeed);
}

void UFlightMovementComponentBase::ApplyCameraTilt()
{
	CurrentCameraPitch = FMath::Clamp(
		CurrentInertialPitch * CameraInertialTiltScale,
		-FMath::Max(CameraPitchOnMove, 0.0f),
		FMath::Max(CameraPitchOnMove, 0.0f)
	);
	CurrentCameraRoll = CurrentInertialRoll * CameraInertialTiltScale;
}

void UFlightMovementComponentBase::ApplyVisualTilt(float DeltaTime)
{
	ApplyMeshTilt(DeltaTime);
	ApplyViewModelTilt(DeltaTime);
}

void UFlightMovementComponentBase::ApplyMeshTilt(float DeltaTime)
{
	USkeletalMeshComponent* VisualMesh = GetFlightVisualMesh();
	if (!bVisualTiltInitialized || !VisualMesh)
	{
		return;
	}

	const float FeelInterpSpeed = CameraRollInterpSpeed / GetMovementMassFactor();
	const FRotator TargetMeshRot =
		BaseMeshRelativeRotation +
		FRotator(
			CurrentInertialPitch * MeshInertialTiltScale,
			0.0f,
			CurrentInertialRoll * MeshInertialTiltScale
		);
	const FRotator NewMeshRot = FMath::RInterpTo(
		VisualMesh->GetRelativeRotation(),
		TargetMeshRot,
		DeltaTime,
		FeelInterpSpeed
	);

	VisualMesh->SetRelativeRotation(NewMeshRot);
}

void UFlightMovementComponentBase::ApplyViewModelTilt(float DeltaTime)
{
	USceneComponent* ViewModelRoot = GetFlightViewModelRoot();
	if (!bViewModelTiltInitialized || !ViewModelRoot)
	{
		return;
	}

	const float FeelInterpSpeed = CameraRollInterpSpeed / GetMovementMassFactor();
	const FRotator TargetViewModelRot =
		BaseViewModelRelativeRotation +
		FRotator(
			CurrentInertialPitch * ViewModelInertialTiltScale,
			0.0f,
			CurrentInertialRoll * ViewModelInertialTiltScale
		);
	const FRotator NewViewModelRot = FMath::RInterpTo(
		ViewModelRoot->GetRelativeRotation(),
		TargetViewModelRot,
		DeltaTime,
		FeelInterpSpeed
	);

	ViewModelRoot->SetRelativeRotation(NewViewModelRot);
}

float UFlightMovementComponentBase::GetMovementMassFactor() const
{
	const ACharacter* OwnerCharacter = GetFlightOwnerCharacter();
	const UCharacterMovementComponent* CharacterMovement = OwnerCharacter
		? OwnerCharacter->GetCharacterMovement()
		: nullptr;

	if (!CharacterMovement)
	{
		return 1.0f;
	}

	return FMath::Clamp(CharacterMovement->Mass / 50.0f, 0.5f, 3.0f);
}

float UFlightMovementComponentBase::ResolveInertialReboundAxis(
	float CurrentValue,
	float DesiredTarget,
	float MaxAbsValue,
	float SpeedAlpha,
	float MassFactor,
	float& PreviousDesiredTarget,
	bool& bReboundActive,
	float& ReboundTarget,
	float& OutInterpSpeedMultiplier) const
{
	OutInterpSpeedMultiplier = 1.0f;

	const float TriggerMagnitude = FMath::Max(MaxAbsValue * 0.25f, 0.75f);
	const float CurrentMagnitude = FMath::Abs(CurrentValue);
	const float DesiredMagnitude = FMath::Abs(DesiredTarget);
	const float PreviousMagnitude = FMath::Abs(PreviousDesiredTarget);
	const bool bHadMeaningfulTilt = CurrentMagnitude > TriggerMagnitude || PreviousMagnitude > TriggerMagnitude;
	const bool bReleased = PreviousMagnitude > TriggerMagnitude && DesiredMagnitude < PreviousMagnitude * 0.45f;
	const bool bReversed = PreviousDesiredTarget * DesiredTarget < -0.1f;

	if (!bReboundActive && bHadMeaningfulTilt && (bReleased || bReversed))
	{
		const float SpeedScale = FMath::Lerp(0.35f, 1.0f, SpeedAlpha);
		const float MassScale = FMath::Clamp(1.0f / FMath::Max(MassFactor, 0.1f), 0.35f, 1.5f);
		const float MaxReboundMagnitude = MaxAbsValue * InertialTiltReboundMaxScale;
		const float ReboundMagnitude = FMath::Clamp(
			CurrentMagnitude * InertialTiltReboundRatio * SpeedScale * MassScale,
			0.0f,
			MaxReboundMagnitude
		);

		ReboundTarget = -FMath::Sign(CurrentValue) * ReboundMagnitude;
		bReboundActive = true;
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltReboundInterpMultiplier, 1.0f);
		PreviousDesiredTarget = DesiredTarget;
		return ReboundTarget;
	}

	if (bReboundActive)
	{
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltReboundInterpMultiplier, 1.0f);
		const float MovementOverrideAlpha = FMath::Clamp(
			DesiredMagnitude / FMath::Max(MaxAbsValue, 1.0f) * FMath::Lerp(0.5f, 1.0f, SpeedAlpha),
			0.0f,
			1.0f
		);
		const float BlendedTarget = FMath::Lerp(ReboundTarget, DesiredTarget, MovementOverrideAlpha);
		const float CompletionTolerance = FMath::Max(MaxAbsValue * 0.04f, 0.2f);

		if (FMath::Abs(CurrentValue - BlendedTarget) <= CompletionTolerance &&
			DesiredMagnitude < TriggerMagnitude * 0.5f)
		{
			bReboundActive = false;
			OutInterpSpeedMultiplier = FMath::Max(InertialTiltRecoverInterpMultiplier, 1.0f);
			PreviousDesiredTarget = DesiredTarget;
			return 0.0f;
		}

		if (MovementOverrideAlpha > 0.85f)
		{
			bReboundActive = false;
		}

		PreviousDesiredTarget = DesiredTarget;
		return BlendedTarget;
	}

	if (DesiredMagnitude < TriggerMagnitude * 0.5f)
	{
		OutInterpSpeedMultiplier = FMath::Max(InertialTiltRecoverInterpMultiplier, 1.0f);
		PreviousDesiredTarget = DesiredTarget;
		return 0.0f;
	}

	PreviousDesiredTarget = DesiredTarget;
	return DesiredTarget;
}
