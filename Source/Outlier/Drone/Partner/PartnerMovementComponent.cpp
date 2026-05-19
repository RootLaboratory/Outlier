// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	bool IsAutoFollowMoveMode(EPartnerMoveMode MoveMode)
	{
		return MoveMode == EPartnerMoveMode::CameraAssist ||
			MoveMode == EPartnerMoveMode::SyncMove;
	}
}

UPartnerMovementComponent::UPartnerMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPartnerMovementComponent::OnRegister()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	Super::OnRegister();

	Activate(true);
	SetComponentTickEnabled(true);
}

void UPartnerMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	Activate(true);
	SetComponentTickEnabled(true);

	if (PartnerCharacter && PartnerCharacter->GetMesh())
	{
		BaseMeshRelativeRotation = PartnerCharacter->GetMesh()->GetRelativeRotation();
		bVisualTiltInitialized = true;
	}

	if (PartnerCharacter && PartnerCharacter->GetFirstPersonViewModelRoot())
	{
		BaseViewModelRelativeRotation = PartnerCharacter->GetFirstPersonViewModelRoot()->GetRelativeRotation();
	}

	RefreshTickEnabled();
}

void UPartnerMovementComponent::RefreshMovementState()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	EDroneMovementState NewState = EDroneMovementState::Follow;

	if (PartnerCharacter->MoveMode == EPartnerMoveMode::SyncMove ||
		PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist)
	{
		NewState = EDroneMovementState::Follow;
	}
	else
	{
		NewState = EDroneMovementState::Fly;
	}
	

	PartnerCharacter->SetMovementState(NewState);
}

void UPartnerMovementComponent::ApplyCameraAssist()
{
	if (!PartnerCharacter || PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist)
	{
		return;
	}

	PartnerCharacter->SetMoveMode(EPartnerMoveMode::CameraAssist);
}

void UPartnerMovementComponent::StopCameraAssist()
{
	if (PartnerCharacter && PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist)
	{
		PartnerCharacter->SetMoveMode(EPartnerMoveMode::Normal);
	}
}

void UPartnerMovementComponent::SetSyncMove(bool SyncMove)
{
	if (!PartnerCharacter)
	{
		return;
	}

	const EPartnerMoveMode TargetMode = SyncMove
		? EPartnerMoveMode::SyncMove
		: EPartnerMoveMode::Normal;

	if (PartnerCharacter->MoveMode == TargetMode)
	{
		return;
	}

	PartnerCharacter->SetMoveMode(TargetMode);
}

void UPartnerMovementComponent::SetFreeMove(bool FreeMove)
{
	if (!PartnerCharacter)
	{
		return;
	}

	if (IsAutoFollowMoveMode(PartnerCharacter->MoveMode))
	{
		return;
	}

	const EPartnerMoveMode TargetMode = FreeMove
		? EPartnerMoveMode::FreeMove
		: EPartnerMoveMode::Normal;

	if (PartnerCharacter->MoveMode == TargetMode)
	{
		return;
	}

	PartnerCharacter->SetMoveMode(TargetMode);
}

void UPartnerMovementComponent::SetMoveInput(const FVector2D& MoveInput)
{
	CurrentMoveInput = PartnerCharacter && IsAutoFollowMoveMode(PartnerCharacter->MoveMode)
		? FVector2D::ZeroVector
		: MoveInput;

	RefreshTickEnabled();
}

void UPartnerMovementComponent::SetVerticalInput(const float Axis)
{
	VerticalInput = PartnerCharacter && IsAutoFollowMoveMode(PartnerCharacter->MoveMode)
		? 0.0f
		: Axis;

	RefreshTickEnabled();
}

void UPartnerMovementComponent::OnMoveModeChanged(EPartnerMoveMode NewMode)
{
	ResetMovementFeel();

	if (IsAutoFollowMoveMode(NewMode))
	{
		CurrentMoveInput = FVector2D::ZeroVector;
		VerticalInput = 0.0f;
	}

	if (NewMode == EPartnerMoveMode::SyncMove && PartnerCharacter &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled()))
	{
		EnterSyncMove();
	}

	RefreshMovementState();
	RefreshTickEnabled();
}

void UPartnerMovementComponent::ResetMovementFeel()
{
	bMovementFeelInitialized = false;
	SmoothedVelocity = PartnerCharacter
		? PartnerCharacter->GetVelocity()
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

void UPartnerMovementComponent::RefreshTickEnabled()
{
	bool bMovementFeelNeedsTick = false;
	if (PartnerCharacter)
	{
		const FRotator MeshRot = PartnerCharacter->GetMesh()
			? PartnerCharacter->GetMesh()->GetRelativeRotation()
			: FRotator::ZeroRotator;

		bMovementFeelNeedsTick =
			!MeshRot.Equals(BaseMeshRelativeRotation, 0.1f) ||
			!FMath::IsNearlyZero(CurrentInertialPitch, 0.1f) ||
			!FMath::IsNearlyZero(CurrentInertialRoll, 0.1f) ||
			!FMath::IsNearlyZero(CurrentCameraPitch, 0.05f) ||
			!FMath::IsNearlyZero(CurrentCameraRoll, 0.05f) ||
			bPitchReboundActive ||
			bRollReboundActive ||
			!SmoothedVelocity.IsNearlyZero(1.0f) ||
			!CurrentMoveInput.IsNearlyZero(0.01f) ||
			!FMath::IsNearlyZero(VerticalInput, 0.01f);
	}

	const bool bShouldTick = PartnerCharacter != nullptr;

	SetComponentTickEnabled(bShouldTick);
}

void UPartnerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PartnerCharacter)
	{
		return;
	}

	const bool bCanRunInputMovement =
		!IsAutoFollowMoveMode(PartnerCharacter->MoveMode) &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled());

	const bool bCanRunServerMovement = PartnerCharacter->HasAuthority();
	const bool bCanRunPredictedFollowMovement =
		PartnerCharacter->IsLocallyControlled() &&
		(
			PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist ||
			PartnerCharacter->MoveMode == EPartnerMoveMode::SyncMove
		);
	const bool bCanRunLocalFeel = PartnerCharacter->IsLocallyControlled();

	if (bCanRunServerMovement && ShooterCharacter)
	{
		UpdateBoundaryState();
	}

	if (bCanRunInputMovement)
	{
		UpdateInputMovement();
	}

	if (bCanRunServerMovement || bCanRunPredictedFollowMovement)
	{
		UpdateServerDrivenMovement(DeltaTime);
	}

	if (bCanRunLocalFeel)
	{
		UpdateMovementFeel(DeltaTime);
	}

	RefreshTickEnabled();
}

void UPartnerMovementComponent::ApplyManualMoveInput()
{
	if (!PartnerCharacter)
	{
		return;
	}

	if (IsAutoFollowMoveMode(PartnerCharacter->MoveMode))
	{
		return;
	}

	if (!PartnerCharacter->HasAuthority() && !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	switch (PartnerCharacter->MoveMode)
	{
	case EPartnerMoveMode::Normal:
		UpdateNormalMove();
		break;
	case EPartnerMoveMode::FreeMove:
		UpdateFreeMove();
		break;
	default:
		break;
	}
}

void UPartnerMovementComponent::UpdateNormalMove()
{
	const float CurrentSpeed = PartnerCharacter->bIsAccelerate
		? PartnerCharacter->BoostSpeed
		: PartnerCharacter->MoveSpeed;

	FVector Move =
		PartnerCharacter->GetActorForwardVector() * CurrentMoveInput.Y +
		PartnerCharacter->GetActorRightVector()   * CurrentMoveInput.X;

	Move.Z = 0.0f;

	if (!FMath::IsNearlyZero(VerticalInput))
	{
		const float VerticalScale = PartnerCharacter->VerticalSpeed / FMath::Max(CurrentSpeed, KINDA_SMALL_NUMBER);
		Move += FVector::UpVector * VerticalInput * VerticalScale;
	}

	if (!Move.IsNearlyZero())
	{
		PartnerCharacter->AddMovementInput(Move.GetClampedToMaxSize(1.0f), 1.0f);
	}
}

void UPartnerMovementComponent::UpdateFreeMove()
{
	const FRotator ViewRot = PartnerCharacter->GetControlRotation();

	const FVector Forward = ViewRot.Vector();
	const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;

	const float CurrentSpeed = PartnerCharacter->bIsAccelerate
		? PartnerCharacter->BoostSpeed
		: PartnerCharacter->MoveSpeed;
	const float VerticalScale = PartnerCharacter->VerticalSpeed / FMath::Max(CurrentSpeed, KINDA_SMALL_NUMBER);

	FVector Move =
		Forward * CurrentMoveInput.Y +
		Right   * CurrentMoveInput.X +
		Up      * VerticalInput * VerticalScale;

	if (Move.IsNearlyZero())
	{
		return;
	}

	PartnerCharacter->AddMovementInput(Move.GetClampedToMaxSize(1.0f), 1.0f);
}

void UPartnerMovementComponent::UpdateSyncMove(float DeltaTime)
{
	if (!ShooterCharacter)
	{
		return;
	}

	const FVector TargetLocation = ShooterCharacter->GetActorLocation() + PartnerCharacter->SyncLocalOffset;

	MoveTowardTargetWithAvoidance(TargetLocation, DeltaTime, PartnerCharacter->SyncMoveInterpSpeed);
}

void UPartnerMovementComponent::UpdateCameraAssist(float DeltaTime)
{
	if (!ShooterCharacter)
	{
		return;
	}

	const FVector ShooterLocation = ShooterCharacter->GetActorLocation();
	const FVector Right = ShooterCharacter->GetActorRightVector();
	const FVector Up = FVector::UpVector;
	const FVector Forward = ShooterCharacter->GetActorForwardVector();

	const FVector LocalOffset = PartnerCharacter->AssistTargetLocalOffset;

	FVector TargetLocation =
		ShooterLocation +
		-Forward * LocalOffset.X +
		Right	 * LocalOffset.Y +
		Up		 * LocalOffset.Z;

	MoveTowardTargetWithAvoidance(TargetLocation, DeltaTime, PartnerCharacter->AssistInterpSpeed);
}

void UPartnerMovementComponent::UpdateMovementFeel(float DeltaTime)
{
	if (!CanUpdateMovementFeel(DeltaTime))
	{
		return;
	}

	if (!bVisualTiltInitialized && PartnerCharacter->GetMesh())
	{
		BaseMeshRelativeRotation = PartnerCharacter->GetMesh()->GetRelativeRotation();
		bVisualTiltInitialized = true;
	}

	const FPartnerMovementKinematics Kinematics = CalculateMovementKinematics(DeltaTime);
	const FPartnerTiltTarget DesiredTarget = CalculateTiltTargets(Kinematics);

	UpdateInertialTilt(DesiredTarget, Kinematics, DeltaTime);
	ApplyCameraTilt();
	ApplyVisualTilt(DeltaTime);

	LastLocation = PartnerCharacter->GetActorLocation();
}

bool UPartnerMovementComponent::CanUpdateMovementFeel(float DeltaTime) const
{
	return PartnerCharacter && DeltaTime > KINDA_SMALL_NUMBER;
}

FPartnerMovementKinematics UPartnerMovementComponent::CalculateMovementKinematics(float DeltaTime)
{
	FPartnerMovementKinematics Result;

	const FVector CurrentLocation = PartnerCharacter->GetActorLocation();
	if (!bMovementFeelInitialized)
	{
		LastLocation = CurrentLocation;
		bMovementFeelInitialized = true;
	}

	const FVector LocationVelocity = (CurrentLocation - LastLocation) / DeltaTime;
	FVector RawVelocity = PartnerCharacter->GetVelocity();
	if (RawVelocity.IsNearlyZero(1.0f) && !LocationVelocity.IsNearlyZero(1.0f))
	{
		RawVelocity = LocationVelocity;
	}

	Result.Acceleration = (RawVelocity - SmoothedVelocity) / DeltaTime;
	Result.Acceleration = Result.Acceleration.GetClampedToMaxSize(
		FMath::Max(PartnerCharacter->Acceleration * 2.0f, 1.0f)
	);

	SmoothedVelocity = FMath::VInterpTo(
		SmoothedVelocity,
		RawVelocity,
		DeltaTime,
		PartnerCharacter->RotationLagRecoverSpeed
	);

	Result.Velocity = RawVelocity;
	Result.LocalVelocity = PartnerCharacter->GetActorTransform().InverseTransformVectorNoScale(RawVelocity);
	Result.LocalAcceleration = PartnerCharacter->GetActorTransform().InverseTransformVectorNoScale(Result.Acceleration);
	Result.MassFactor = GetMovementMassFactor();

	const float SpeedReference = FMath::Max(PartnerCharacter->BoostSpeed, PartnerCharacter->MoveSpeed);
	Result.SpeedAlpha = FMath::Clamp(
		Result.LocalVelocity.Size() / FMath::Max(SpeedReference, 1.0f),
		0.0f,
		1.0f
	);

	return Result;
}

FPartnerTiltTarget UPartnerMovementComponent::CalculateTiltTargets(const FPartnerMovementKinematics& Kinematics) const
{
	FPartnerTiltTarget Result;

	const float AccelReference = FMath::Max(PartnerCharacter->Acceleration, 1.0f);
	Result.TiltStrength = FMath::Clamp(PartnerCharacter->RotationLagAmount * 12.5f, 0.1f, 3.0f);

	const float MaxRoll = PartnerCharacter->GetMaxInertialCameraRollDegrees();
	const float MaxPitch = PartnerCharacter->GetMaxInertialCameraPitchDegrees();
	Result.EffectiveMaxPitch = MaxPitch / Kinematics.MassFactor;
	Result.EffectiveMaxRoll = MaxRoll / Kinematics.MassFactor;

	const bool bHasMovementInput =
		!CurrentMoveInput.IsNearlyZero(0.01f) ||
		!FMath::IsNearlyZero(VerticalInput, 0.01f);
	const float PitchResponseScale = bHasMovementInput ? 1.0f : 0.5f;

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
			Kinematics.LocalVelocity.Y / FMath::Max(PartnerCharacter->MoveSpeed, 1.0f),
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

void UPartnerMovementComponent::UpdateInertialTilt(
	const FPartnerTiltTarget& TiltTarget,
	const FPartnerMovementKinematics& Kinematics,
	float DeltaTime)
{
	float PitchInterpSpeedMultiplier = 1.0f;
	float RollInterpSpeedMultiplier = 1.0f;

	const float TargetBlendSpeed =
		FMath::Max(PartnerCharacter->CameraRollInterpSpeed, 1.0f);
	SmoothedTiltTarget.X = FMath::FInterpTo(
		SmoothedTiltTarget.X,
		TiltTarget.Pitch,
		DeltaTime,
		TargetBlendSpeed
	);
	SmoothedTiltTarget.Y = FMath::FInterpTo(
		SmoothedTiltTarget.Y,
		TiltTarget.Roll,
		DeltaTime,
		TargetBlendSpeed
	);

	FPartnerTiltTarget ResolvedTarget = TiltTarget;
	ResolvedTarget.Pitch = SmoothedTiltTarget.X;
	ResolvedTarget.Roll = SmoothedTiltTarget.Y;
	ResolvedTarget.Pitch = ResolveInertialReboundAxis(
		CurrentInertialPitch,
		ResolvedTarget.Pitch,
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
		ResolvedTarget.Roll,
		TiltTarget.EffectiveMaxRoll,
		Kinematics.SpeedAlpha,
		Kinematics.MassFactor,
		PreviousTargetRoll,
		bRollReboundActive,
		RollReboundTarget,
		RollInterpSpeedMultiplier
	);

	const float CoupledInterpSpeedMultiplier =
		FMath::Max(PitchInterpSpeedMultiplier, RollInterpSpeedMultiplier);
	const float FeelInterpSpeed =
		PartnerCharacter->CameraRollInterpSpeed / Kinematics.MassFactor * CoupledInterpSpeedMultiplier;

	CurrentInertialPitch = FMath::FInterpTo(
		CurrentInertialPitch,
		ResolvedTarget.Pitch,
		DeltaTime,
		FeelInterpSpeed
	);

	CurrentInertialRoll = FMath::FInterpTo(
		CurrentInertialRoll,
		ResolvedTarget.Roll,
		DeltaTime,
		FeelInterpSpeed
	);
}

void UPartnerMovementComponent::ApplyCameraTilt()
{
	CurrentCameraPitch = FMath::Clamp(
		CurrentInertialPitch * PartnerCharacter->CameraInertialTiltScale,
		-PartnerCharacter->GetMaxInertialCameraPitchDegrees(),
		PartnerCharacter->GetMaxInertialCameraPitchDegrees()
	);
	CurrentCameraRoll = CurrentInertialRoll * PartnerCharacter->CameraInertialTiltScale;
}

void UPartnerMovementComponent::ApplyVisualTilt(float DeltaTime)
{
	ApplyMeshTilt(DeltaTime);
	ApplyViewModelTilt(DeltaTime);
}

void UPartnerMovementComponent::ApplyMeshTilt(float DeltaTime)
{
	if (!bVisualTiltInitialized || !PartnerCharacter->GetMesh())
	{
		return;
	}

	const float FeelInterpSpeed = PartnerCharacter->CameraRollInterpSpeed / GetMovementMassFactor();
	const FRotator TargetMeshRot =
		BaseMeshRelativeRotation +
		FRotator(
			CurrentInertialPitch * PartnerCharacter->MeshInertialTiltScale,
			0.0f,
			CurrentInertialRoll * PartnerCharacter->MeshInertialTiltScale
		);

	const FRotator NewMeshRot = FMath::RInterpTo(
		PartnerCharacter->GetMesh()->GetRelativeRotation(),
		TargetMeshRot,
		DeltaTime,
		FeelInterpSpeed
	);

	PartnerCharacter->GetMesh()->SetRelativeRotation(NewMeshRot);
}

void UPartnerMovementComponent::ApplyViewModelTilt(float DeltaTime)
{
	USceneComponent* ViewModelRoot = PartnerCharacter->GetFirstPersonViewModelRoot();
	if (!ViewModelRoot)
	{
		return;
	}

	const float FeelInterpSpeed = PartnerCharacter->CameraRollInterpSpeed / GetMovementMassFactor();
	const FRotator TargetViewModelRot =
		BaseViewModelRelativeRotation +
		FRotator(
			CurrentInertialPitch * PartnerCharacter->ViewModelInertialTiltScale,
			0.0f,
			CurrentInertialRoll * PartnerCharacter->ViewModelInertialTiltScale
		);

	const FRotator NewViewModelRot = FMath::RInterpTo(
		ViewModelRoot->GetRelativeRotation(),
		TargetViewModelRot,
		DeltaTime,
		FeelInterpSpeed
	);

	ViewModelRoot->SetRelativeRotation(NewViewModelRot);
}

void UPartnerMovementComponent::UpdateBoundaryState()
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	const float Distance = FVector::Dist(
		PartnerCharacter->GetActorLocation(),
		ShooterCharacter->GetActorLocation()
	);

	const bool bOutside = Distance > PartnerCharacter->SuitDisableBoundaryRadius;

	PartnerCharacter->SetBoundaryOutside(bOutside);
}

void UPartnerMovementComponent::EnterSyncMove()
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	PartnerCharacter->SyncLocalOffset = PartnerCharacter->GetActorLocation() - ShooterCharacter->GetActorLocation();
}

void UPartnerMovementComponent::MoveTowardTargetWithAvoidance(
	const FVector& TargetLocation,
	float DeltaTime,
	float InterpSpeed)
{
	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector CurrentLocation = PartnerCharacter->GetActorLocation();

	FHitResult Hit;
	const bool bBlocked = GetWorld()->SweepSingleByChannel(
		Hit,
		CurrentLocation,
		TargetLocation,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(40.0f)
	);

	FVector MoveTarget = TargetLocation;

	if (bBlocked)
	{
		MoveTarget = FindSimpleAvoidanceTarget(
			CurrentLocation,
			TargetLocation,
			Hit
		);
	}

	const FVector ToTarget = MoveTarget - CurrentLocation;
	const float Distance = ToTarget.Size();
	const float AcceptanceRadius = PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist
		? FMath::Max(PartnerCharacter->AssistMinDistance, 10.0f)
		: 25.0f;

	if (Distance <= AcceptanceRadius)
	{
		return;
	}

	const float MaxDistance = PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist
		? FMath::Max(PartnerCharacter->AssistMaxDistance, AcceptanceRadius + 1.0f)
		: FMath::Max(PartnerCharacter->SyncMoveDistance, AcceptanceRadius + 1.0f);

	const float DistanceAlpha = FMath::Clamp(
		(Distance - AcceptanceRadius) / FMath::Max(MaxDistance - AcceptanceRadius, 1.0f),
		0.0f,
		1.0f
	);
	const float StrengthScale = PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist
		? FMath::Clamp(PartnerCharacter->AssistStrength / 100.0f, 0.0f, 1.0f)
		: 1.0f;
	const float InterpScale = FMath::Clamp(InterpSpeed / 8.0f, 0.1f, 1.0f);
	const float InputScale = FMath::Clamp(DistanceAlpha * StrengthScale * InterpScale, 0.0f, 1.0f);

	PartnerCharacter->AddMovementInput(ToTarget.GetSafeNormal(), InputScale);
}

FVector UPartnerMovementComponent::FindSimpleAvoidanceTarget(const FVector& CurrentLocation, const FVector& TargetLocation, const FHitResult& Hit)
{
	const FVector ToTarget = (TargetLocation - CurrentLocation).GetSafeNormal();
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, ToTarget).GetSafeNormal();

	const float AvoidDistance = 150.0f;

	TArray<FVector> Candidates;
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint + Up * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint - Up * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistance + Up * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistance - Up * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistance + Up * AvoidDistance);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistance - Up * AvoidDistance);

	FVector BestTarget = CurrentLocation;
	float BestScore = TNumericLimits<float>::Max();

	for (const FVector& Candidate : Candidates)
	{
		if (!IsPathClear(CurrentLocation, Candidate))
		{
			continue;
		}

		const float DistanceScore =
			FVector::DistSquared(CurrentLocation, Candidate) +
			FVector::DistSquared(Candidate, TargetLocation);		// 거리 점수

		FVector ToCandidate =
			(Candidate - CurrentLocation).GetSafeNormal();

		const float DirectionScore =
			1.0f - FVector::DotProduct(ToCandidate, ToTarget);		// 방향 점수

		const float Score = DistanceScore + DirectionScore * DirectionWeight;

		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

bool UPartnerMovementComponent::IsPathClear(const FVector& From, const FVector& To) const
{
	FHitResult Hit;

	return !GetWorld()->SweepSingleByChannel(
		Hit,
		From,
		To,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(40.0f)
	);
}

float UPartnerMovementComponent::GetMovementMassFactor() const
{
	const UCharacterMovementComponent* CharacterMovement = PartnerCharacter
		? PartnerCharacter->GetCharacterMovement()
		: nullptr;

	if (!CharacterMovement)
	{
		return 1.0f;
	}

	return FMath::Clamp(CharacterMovement->Mass / 100.0f, 0.5f, 3.0f);
}

float UPartnerMovementComponent::ResolveInertialReboundAxis(
	float CurrentValue,
	float DesiredTarget,
	float MaxAbsValue,
	float SpeedAlpha,
	float MassFactor,
	float& PreviousDesiredTarget,
	bool& bReboundActive,
	float& ReboundTarget,
	float& OutInterpSpeedMultiplier
) const
{
	if (!PartnerCharacter)
	{
		return DesiredTarget;
	}

	OutInterpSpeedMultiplier = 1.0f;

	const float TriggerMagnitude = FMath::Max(MaxAbsValue * 0.25f, 0.75f);
	const float CurrentMagnitude = FMath::Abs(CurrentValue);
	const float DesiredMagnitude = FMath::Abs(DesiredTarget);
	const float PreviousMagnitude = FMath::Abs(PreviousDesiredTarget);

	const bool bHadMeaningfulTilt =
		CurrentMagnitude > TriggerMagnitude ||
		PreviousMagnitude > TriggerMagnitude;

	const bool bReleased =
		PreviousMagnitude > TriggerMagnitude &&
		DesiredMagnitude < PreviousMagnitude * 0.45f;

	const bool bReversed =
		PreviousDesiredTarget * DesiredTarget < -0.1f;

	if (!bReboundActive && bHadMeaningfulTilt && (bReleased || bReversed))
	{
		const float ReboundRatio =
			FMath::Clamp(PartnerCharacter->InertialTiltReboundRatio, 0.0f, 1.0f);

		const float SpeedScale = FMath::Lerp(0.35f, 1.0f, SpeedAlpha);
		const float MassScale = FMath::Clamp(1.0f / FMath::Max(MassFactor, 0.1f), 0.35f, 1.5f);

		const float MaxReboundMagnitude =
			MaxAbsValue * PartnerCharacter->InertialTiltReboundMaxScale;

		const float ReboundMagnitude = FMath::Clamp(
			CurrentMagnitude * ReboundRatio * SpeedScale * MassScale,
			0.0f,
			MaxReboundMagnitude);

		ReboundTarget =
			-FMath::Sign(CurrentValue) * ReboundMagnitude;

		bReboundActive = true;
		OutInterpSpeedMultiplier =
			FMath::Max(PartnerCharacter->InertialTiltReboundInterpMultiplier, 1.0f);

		PreviousDesiredTarget = DesiredTarget;
		return ReboundTarget;
	}

	if (bReboundActive)
	{
		OutInterpSpeedMultiplier =
			FMath::Max(PartnerCharacter->InertialTiltReboundInterpMultiplier, 1.0f);

		const float MovementOverrideAlpha = FMath::Clamp(
			DesiredMagnitude / FMath::Max(MaxAbsValue, 1.0f) * FMath::Lerp(0.5f, 1.0f, SpeedAlpha),
			0.0f,
			1.0f
		);
		const float BlendedTarget = FMath::Lerp(
			ReboundTarget,
			DesiredTarget,
			MovementOverrideAlpha
		);

		const float CompletionTolerance =
			FMath::Max(MaxAbsValue * 0.04f, 0.2f);

		if (FMath::Abs(CurrentValue - BlendedTarget) <= CompletionTolerance &&
			DesiredMagnitude < TriggerMagnitude * 0.5f)
		{
			bReboundActive = false;
			OutInterpSpeedMultiplier =
				FMath::Max(PartnerCharacter->InertialTiltRecoverInterpMultiplier, 1.0f);

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
		OutInterpSpeedMultiplier =
			FMath::Max(PartnerCharacter->InertialTiltRecoverInterpMultiplier, 1.0f);

		PreviousDesiredTarget = DesiredTarget;
		return 0.0f;
	}

	PreviousDesiredTarget = DesiredTarget;
	return DesiredTarget;
}

void UPartnerMovementComponent::UpdateInputMovement()
{
	if (!PartnerCharacter || IsAutoFollowMoveMode(PartnerCharacter->MoveMode))
	{
		return;
	}

	switch (PartnerCharacter->MoveMode)
	{
	case EPartnerMoveMode::Normal:
		UpdateNormalMove();
		break;
	case EPartnerMoveMode::FreeMove:
		UpdateFreeMove();
		break;
	default:
		break;
	}
}

void UPartnerMovementComponent::UpdateServerDrivenMovement(float DeltaTime)
{
	if (!ShooterCharacter)
	{
		return;
	}

	switch (PartnerCharacter->MoveMode)
	{
	case EPartnerMoveMode::CameraAssist:
		UpdateCameraAssist(DeltaTime);
		break;
	case EPartnerMoveMode::SyncMove:
		UpdateSyncMove(DeltaTime);
		break;
	default:
		break;
	}
}
