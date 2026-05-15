// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"


void UPartnerMovementComponent::RefreshMovementState()
{
	if (!PartnerCharacter)
	{
		return;
	}

	EDroneMovementState NewState = EDroneMovementState::Follow;

}

void UPartnerMovementComponent::ApplyCameraAssist()
{
	bCameraAssist = true;
	PartnerCharacter->SetMoveMode(EPartnerMoveMode::CameraAssist);
}

void UPartnerMovementComponent::StopCameraAssist()
{
	bCameraAssist = false;
	if (PartnerCharacter && PartnerCharacter->MoveMode == EPartnerMoveMode::CameraAssist)
	{
		PartnerCharacter->SetMoveMode(EPartnerMoveMode::Normal);
	}
}

void UPartnerMovementComponent::SetSyncMove(bool SyncMove)
{
	bSyncMove = SyncMove;
	if (PartnerCharacter)
	{
		PartnerCharacter->SetMoveMode(SyncMove ? EPartnerMoveMode::SyncMove : EPartnerMoveMode::Normal);
	}
}

void UPartnerMovementComponent::SetFreeMove(bool FreeMove)
{
	bFreeMove = FreeMove;
	if (PartnerCharacter)
	{
		PartnerCharacter->SetMoveMode(FreeMove ? EPartnerMoveMode::FreeMove : EPartnerMoveMode::Normal);
	}
}

void UPartnerMovementComponent::SetMoveInput(const FVector2D& MoveInput)
{
	CurrentMoveInput = MoveInput;
	RefreshTickEnabled();
}

void UPartnerMovementComponent::SetVerticalInput(const float Axis)
{
	VerticalInput = Axis;
	RefreshTickEnabled();
}

void UPartnerMovementComponent::OnMoveModeChanged(EPartnerMoveMode NewMode)
{
	if (NewMode == EPartnerMoveMode::SyncMove)
	{
		EnterSyncMove();
	}

	RefreshTickEnabled();
}

void UPartnerMovementComponent::RefreshTickEnabled()
{
	bool bMovementFeelNeedsTick = false;
	if (PartnerCharacter && PartnerCharacter->GetFirstPersonCameraRoot())
	{
		const FRotator CameraRootRot = PartnerCharacter->GetFirstPersonCameraRoot()->GetRelativeRotation();
		bMovementFeelNeedsTick =
			!CameraRootRot.IsNearlyZero(0.1f) ||
			!SmoothedVelocity.IsNearlyZero(1.0f) ||
			!FMath::IsNearlyZero(PartnerCharacter->LookRollInput, 0.01f);
	}

	const bool bShouldTick =
	  PartnerCharacter &&
	  (
		PartnerCharacter->MoveMode		== EPartnerMoveMode::SyncMove	  ||
		PartnerCharacter->MoveMode		== EPartnerMoveMode::CameraAssist ||
		PartnerCharacter->BoundaryState != EPartnerBoundaryState::Inside  ||
		!CurrentMoveInput.IsNearlyZero()								  ||
		!FMath::IsNearlyZero(VerticalInput)								  ||
		bMovementFeelNeedsTick
	  );

	SetComponentTickEnabled(bShouldTick);
}

void UPartnerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PartnerCharacter || !ShooterCharacter)
	{
		return;
	}

	UpdateBoundaryState();

	switch (PartnerCharacter->MoveMode)
	{
	case EPartnerMoveMode::Normal:
		UpdateNormalMove(DeltaTime);
		break;
	case EPartnerMoveMode::FreeMove:
		UpdateFreeMove(DeltaTime);
		break;
	case EPartnerMoveMode::SyncMove:
		UpdateSyncMove(DeltaTime);
		break;
	case EPartnerMoveMode::CameraAssist:
		UpdateCameraAssist(DeltaTime);
		break;
	}

	UpdateMovementFeel(DeltaTime);
	RefreshTickEnabled();
}

void UPartnerMovementComponent::UpdateNormalMove(float DeltaTime)
{
	FVector Move =
		PartnerCharacter->GetActorForwardVector() * CurrentMoveInput.X +
		PartnerCharacter->GetActorRightVector()   * CurrentMoveInput.Y;

	Move.Z = 0.0f;

	if (!Move.IsNearlyZero())
	{
		PartnerCharacter->AddMovementInput(Move.GetSafeNormal(), 1.0f);
	}

	if (!FMath::IsNearlyZero(VerticalInput))
	{
		FVector Delta = FVector::UpVector
			* VerticalInput
			* PartnerCharacter->VerticalSpeed
			* DeltaTime;

		PartnerCharacter->AddActorWorldOffset(Delta, true);
	}
}

void UPartnerMovementComponent::UpdateFreeMove(float DeltaTime)
{
	const FRotator ViewRot = PartnerCharacter->GetControlRotation();

	const FVector Forward = ViewRot.Vector();
	const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::X);
	const FVector Up = FVector::UpVector;

	FVector Move =
		Forward * CurrentMoveInput.X +
		Right   * CurrentMoveInput.Y +
		Up      * VerticalInput;

	if (Move.IsNearlyZero())
	{
		return;
	}

	const float Speed = PartnerCharacter->bIsAccelerate
		? PartnerCharacter->BoostSpeed
		: PartnerCharacter->MoveSpeed;

	PartnerCharacter->AddActorWorldOffset(
		Move.GetSafeNormal() * Speed * DeltaTime,
		true
	);
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

void UPartnerMovementComponent::UpdateVerticalMove(float DeltaTime)
{
	if (FMath::IsNearlyZero(VerticalInput))
	{
		return;
	}

	FVector VerticalOffset =
		FVector::UpVector * VerticalInput * PartnerCharacter->VerticalSpeed * DeltaTime;

	PartnerCharacter->AddActorWorldOffset(VerticalOffset, true);
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

	FVector NewLocation = FMath::VInterpTo(
		PartnerCharacter->GetActorLocation(),
		TargetLocation,
		DeltaTime,
		PartnerCharacter->AssistInterpSpeed
	);

	MoveTowardTargetWithAvoidance(NewLocation, DeltaTime, PartnerCharacter->AssistInterpSpeed);
}

void UPartnerMovementComponent::UpdateMovementFeel(float DeltaTime)
{
	if (!PartnerCharacter || DeltaTime <= KINDA_SMALL_NUMBER || !PartnerCharacter->GetFirstPersonCameraRoot())
	{
		return;
	}

	const FVector CurrentLocation = PartnerCharacter->GetActorLocation();
	if (!bMovementFeelInitialized)
	{
		LastLocation = CurrentLocation;
		bMovementFeelInitialized = true;
		return;
	}

	const FVector RawVelocity = (CurrentLocation - LastLocation) / DeltaTime;
	const FVector Acceleration = (RawVelocity - SmoothedVelocity) / DeltaTime;

	SmoothedVelocity = FMath::VInterpTo(
		SmoothedVelocity,
		RawVelocity,
		DeltaTime,
		PartnerCharacter->RotationLagRecoverSpeed
	);

	const FVector LocalAcceleration =
		PartnerCharacter->GetActorTransform().InverseTransformVectorNoScale(Acceleration);

	const float MassFactor = GetMovementMassFactor();
	const float AccelScale = PartnerCharacter->RotationLagAmount * 0.002f / MassFactor;
	const float MaxPitch = 10.0f;
	const float MaxRoll = 15.0f;

	const float AccelPitch = FMath::Clamp(
		-LocalAcceleration.X * AccelScale,
		-MaxPitch,
		MaxPitch
	);

	const float AccelRoll = FMath::Clamp(
		LocalAcceleration.Y * AccelScale,
		-MaxRoll,
		MaxRoll
	);

	float TurnRoll = 0.0f;
	if (!RawVelocity.IsNearlyZero(1.0f))
	{
		const FVector VelocityDir = RawVelocity.GetSafeNormal2D();
		const FVector ForwardDir = PartnerCharacter->GetActorForwardVector().GetSafeNormal2D();

		if (!VelocityDir.IsNearlyZero() && !ForwardDir.IsNearlyZero())
		{
			const float Dot = FMath::Clamp(FVector::DotProduct(ForwardDir, VelocityDir), -1.0f, 1.0f);
			const float CrossZ = FVector::CrossProduct(ForwardDir, VelocityDir).Z;

			TurnRoll = FMath::RadiansToDegrees(FMath::Acos(Dot)) * CrossZ * 0.25f / MassFactor;
		}
	}

	const float LookRoll = -PartnerCharacter->LookRollInput * PartnerCharacter->CameraRollOnTurn / MassFactor;
	const float TargetPitch = FMath::Clamp(AccelPitch, -MaxPitch, MaxPitch);
	const float TargetRoll = FMath::Clamp(AccelRoll + TurnRoll + LookRoll, -MaxRoll, MaxRoll);

	FRotator CurrentRot = PartnerCharacter->GetFirstPersonCameraRoot()->GetRelativeRotation();

	const FRotator TargetRot(TargetPitch, 0.0f, TargetRoll);
	const float FeelInterpSpeed = PartnerCharacter->CameraRollInterpSpeed / MassFactor;
	const FRotator NewRot = FMath::RInterpTo(
		CurrentRot,
		TargetRot,
		DeltaTime,
		FeelInterpSpeed
	);

	PartnerCharacter->GetFirstPersonCameraRoot()->SetRelativeRotation(NewRot);
	PartnerCharacter->LookRollInput = FMath::FInterpTo(
		PartnerCharacter->LookRollInput,
		0.0f,
		DeltaTime,
		PartnerCharacter->RotationLagRecoverSpeed / MassFactor
	);

	LastLocation = CurrentLocation;
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

	const FVector NewLocation = FMath::VInterpTo(
		CurrentLocation,
		MoveTarget,
		DeltaTime,
		InterpSpeed
	);

	PartnerCharacter->SetActorLocation(NewLocation, true);
}

FVector UPartnerMovementComponent::FindSimpleAvoidanceTarget(const FVector& CurrentLocation, const FVector& TargetLocation, const FHitResult& Hit)
{
	const FVector ToTarget = (TargetLocation - CurrentLocation).GetSafeNormal();
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, ToTarget).GetSafeNormal();

	const float AvoidDistace = 150.0f;

	TArray<FVector> Candidates;
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint + Up * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint - Up * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistace + Up * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint + Right * AvoidDistace - Up * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistace + Up * AvoidDistace);
	Candidates.Add(Hit.ImpactPoint - Right * AvoidDistace - Up * AvoidDistace);

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
