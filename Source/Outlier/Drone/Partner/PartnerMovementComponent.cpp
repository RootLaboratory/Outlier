#include "Drone/Partner/PartnerMovementComponent.h"

#include "CollisionShape.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

UPartnerMovementComponent::UPartnerMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPartnerMovementComponent::BeginPlay()
{
	RefreshCharacterRefsFromPlayerState();
	ApplyPartnerFlightSettings();

	Super::BeginPlay();

	RefreshMovementState();
}

void UPartnerMovementComponent::RefreshCharacterRefsFromPlayerState()
{
	PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		ShooterCharacter = nullptr;
		return;
	}

	AOutlierPlayerState* PS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>();
	ShooterCharacter = PS ? PS->GetShooterCharacter() : nullptr;
}

void UPartnerMovementComponent::RefreshMovementState()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	const EDroneMovementState NewState = IsAutoFollowMoveMode()
		? EDroneMovementState::Follow
		: EDroneMovementState::Fly;
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
	if (!PartnerCharacter || IsAutoFollowMoveMode())
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
	Super::SetMoveInput(IsAutoFollowMoveMode() ? FVector2D::ZeroVector : MoveInput);
}

void UPartnerMovementComponent::SetVerticalInput(float Axis)
{
	Super::SetVerticalInput(IsAutoFollowMoveMode() ? 0.0f : Axis);
}

void UPartnerMovementComponent::OnMoveModeChanged(EPartnerMoveMode NewMode)
{
	ResetMovementFeel();

	if (NewMode == EPartnerMoveMode::SyncMove && PartnerCharacter &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled()))
	{
		EnterSyncMove();
	}

	if (IsAutoFollowMoveMode())
	{
		ClearFlightInput();
	}

	RefreshMovementState();
	RefreshTickEnabled();
}

void UPartnerMovementComponent::ApplyPartnerFlightSettings()
{
	if (!PartnerCharacter)
	{
		return;
	}

	SetMoveSpeed(PartnerCharacter->MoveSpeed);
	SetBoostSpeed(PartnerCharacter->BoostSpeed);
	SetVerticalSpeed(PartnerCharacter->VerticalSpeed);
	SetAcceleration(PartnerCharacter->Acceleration);
	SetDeceleration(PartnerCharacter->Deceleration);
	SetRotationLagAmount(PartnerCharacter->RotationLagAmount);
	SetRotationLagRecoverSpeed(PartnerCharacter->RotationLagRecoverSpeed);
	SetCameraPitchOnMove(PartnerCharacter->CameraPitchOnMove);
	SetCameraRollOnTurn(PartnerCharacter->CameraRollOnTurn);
	SetCameraRollInterpSpeed(PartnerCharacter->CameraRollInterpSpeed);
	SetMeshInertialTiltScale(PartnerCharacter->MeshInertialTiltScale);
	SetCameraInertialTiltScale(PartnerCharacter->CameraInertialTiltScale);
	SetViewModelInertialTiltScale(PartnerCharacter->ViewModelInertialTiltScale);
	SetInertialTiltReboundRatio(PartnerCharacter->InertialTiltReboundRatio);
	SetInertialTiltReboundMaxScale(PartnerCharacter->InertialTiltReboundMaxScale);
	SetInertialTiltReboundInterpMultiplier(PartnerCharacter->InertialTiltReboundInterpMultiplier);
	SetInertialTiltRecoverInterpMultiplier(PartnerCharacter->InertialTiltRecoverInterpMultiplier);
	SetAccelerating(PartnerCharacter->bIsAccelerate);
}

ACharacter* UPartnerMovementComponent::GetFlightOwnerCharacter() const
{
	return PartnerCharacter;
}

USceneComponent* UPartnerMovementComponent::GetFlightVisualTiltRoot() const
{
	return PartnerCharacter ? PartnerCharacter->GetThirdPersonTiltRoot() : nullptr;
}

USceneComponent* UPartnerMovementComponent::GetFlightViewModelRoot() const
{
	return PartnerCharacter ? PartnerCharacter->GetFirstPersonViewModelRoot() : nullptr;
}

bool UPartnerMovementComponent::CanRunInputMovement() const
{
	return PartnerCharacter &&
		!IsAutoFollowMoveMode() &&
		(PartnerCharacter->HasAuthority() || PartnerCharacter->IsLocallyControlled());
}

bool UPartnerMovementComponent::ShouldUpdateMovementFeel() const
{
	if (!PartnerCharacter || PartnerCharacter->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	return PartnerCharacter->IsLocallyControlled() || PartnerCharacter->WasRecentlyRendered();
}

EFlightInputMode UPartnerMovementComponent::GetFlightInputMode() const
{
	return PartnerCharacter && PartnerCharacter->MoveMode == EPartnerMoveMode::Normal
		? EFlightInputMode::Horizontal
		: EFlightInputMode::Free;
}

void UPartnerMovementComponent::OnAfterInputMovement(float DeltaTime)
{
	if (!PartnerCharacter)
	{
		return;
	}

	const bool bCanRunServerMovement = PartnerCharacter->HasAuthority();
	const bool bCanRunPredictedFollowMovement =
		PartnerCharacter->IsLocallyControlled() && IsAutoFollowMoveMode();

	if (!bCanRunServerMovement && !bCanRunPredictedFollowMovement)
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

bool UPartnerMovementComponent::IsAutoFollowMoveMode() const
{
	return PartnerCharacter &&
		(
			PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist ||
			PartnerCharacter->MoveMode == EPartnerMoveMode::SyncMove
		);
}

void UPartnerMovementComponent::EnterSyncMove()
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	PartnerCharacter->SyncLocalOffset = PartnerCharacter->GetActorLocation() - ShooterCharacter->GetActorLocation();
}

void UPartnerMovementComponent::UpdateSyncMove(float DeltaTime)
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	const FVector TargetLocation = ShooterCharacter->GetActorLocation() + PartnerCharacter->SyncLocalOffset;
	MoveTowardTargetWithAvoidance(TargetLocation, DeltaTime, PartnerCharacter->SyncMoveInterpSpeed);
}

void UPartnerMovementComponent::UpdateCameraAssist(float DeltaTime)
{
	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	const FVector ShooterLocation = ShooterCharacter->GetActorLocation();
	const FVector Right = ShooterCharacter->GetActorRightVector();
	const FVector Up = FVector::UpVector;
	const FVector Forward = ShooterCharacter->GetActorForwardVector();
	const FVector LocalOffset = PartnerCharacter->AssistTargetLocalOffset;
	const FVector TargetLocation =
		ShooterLocation +
		-Forward * LocalOffset.X +
		Right * LocalOffset.Y +
		Up * LocalOffset.Z;

	MoveTowardTargetWithAvoidance(TargetLocation, DeltaTime, PartnerCharacter->AssistInterpSpeed);
}

void UPartnerMovementComponent::MoveTowardTargetWithAvoidance(
	const FVector& TargetLocation,
	float DeltaTime,
	float InterpSpeed)
{
	if (!PartnerCharacter || DeltaTime <= KINDA_SMALL_NUMBER)
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
		MoveTarget = FindSimpleAvoidanceTarget(CurrentLocation, TargetLocation, Hit);
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

FVector UPartnerMovementComponent::FindSimpleAvoidanceTarget(
	const FVector& CurrentLocation,
	const FVector& TargetLocation,
	const FHitResult& Hit)
{
	const FVector ToTarget = (TargetLocation - CurrentLocation).GetSafeNormal();
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, ToTarget).GetSafeNormal();
	const float AvoidDistance = 150.0f;

	TArray<FVector, TInlineAllocator<8>> Candidates;
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
			FVector::DistSquared(Candidate, TargetLocation);
		const FVector ToCandidate = (Candidate - CurrentLocation).GetSafeNormal();
		const float DirectionScore = 1.0f - FVector::DotProduct(ToCandidate, ToTarget);
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
