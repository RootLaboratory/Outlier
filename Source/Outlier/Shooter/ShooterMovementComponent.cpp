// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"

UShooterMovementComponent::UShooterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UShooterMovementComponent::TryStartSprint()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetSprintState(true);
	}

	if (ShooterCharacter->bIsDead || ShooterCharacter->GetCharacterMovement()->IsCrouching() || ShooterCharacter->bAimHeld || ShooterCharacter->bIsReloading)
	{
		return;
	}

	ShooterCharacter->bSprintHeld = true;
	ShooterCharacter->bIsSprinting = true;
	ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = ShooterCharacter->SprintSpeed;
	RefreshMovementState();
}

void UShooterMovementComponent::TryStopSprint()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetSprintState(false);
	}

	ShooterCharacter->bSprintHeld = false;
	StopSprintInternal();
	RefreshMovementState();
}

void UShooterMovementComponent::TryStartCrouchOrSlide()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerStartCrouchOrSlide();
	}

	if (ShooterCharacter->bIsDead)
	{
		return;
	}

	ShooterCharacter->bCrouchHeld = true;

	if (ShooterCharacter->bIsSprinting && ShooterCharacter->GetVelocity().SizeSquared() > 0.0f)
	{
		TrySlide();
		return;
	}

	ShooterCharacter->Crouch();
	SetMovementStateImmediate(EMovementState::Sit);
}

void UShooterMovementComponent::TryStopCrouch()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerStopCrouch();
	}

	ShooterCharacter->bCrouchHeld = false;

	if (ShooterCharacter->bIsSliding)
	{
		return;
	}

	ShooterCharacter->UnCrouch();

	if (!ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		SetMovementStateImmediate(EMovementState::Jump);
	}
	else
	{
		const float Speed2D = ShooterCharacter->GetVelocity().Size2D();
		SetMovementStateImmediate(
			Speed2D <= KINDA_SMALL_NUMBER
				? EMovementState::Idle
				: (ShooterCharacter->bIsSprinting ? EMovementState::Run : EMovementState::Walk));
	}
}

void UShooterMovementComponent::TrySlide()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !CanStartSlide())
	{
		return;
	}

	const FVector Velocity2D = ShooterCharacter->GetVelocity().GetSafeNormal2D();
	if (Velocity2D.IsNearlyZero())
	{
		return;
	}

	// 슬라이드는 이동 상태, 웅크림, 타이머가 함께 바뀌는 복합 액션이다.
	ShooterCharacter->bIsSliding = true;
	ShooterCharacter->SlideDirection = Velocity2D;
	ShooterCharacter->SlideStartSpeed = ShooterCharacter->SprintSpeed * ShooterCharacter->SlideSpeedMultiplier;
	ShooterCharacter->SlideElapsedTime = 0.0f;

	ShooterCharacter->Crouch();
	RefreshMovementState();
	ShooterCharacter->PlayAnimMontage(ShooterCharacter->SlideMontage);

	StartSlideMovement();

	FTimerDelegate SlideEndDelegate;
	SlideEndDelegate.BindUObject(ShooterCharacter, &AShooterCharacter::StopSlide, ESlideEndReason::Finished);

	ShooterCharacter->GetWorldTimerManager().SetTimer(
		ShooterCharacter->SlideTimerHandle,
		SlideEndDelegate,
		ShooterCharacter->SlideDuration,
		false);
}

void UShooterMovementComponent::StopSlide(ESlideEndReason EndReason)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->bIsSliding)
	{
		return;
	}

	ShooterCharacter->bIsSliding = false;
	ShooterCharacter->GetWorldTimerManager().ClearTimer(ShooterCharacter->SlideTimerHandle);
	FinishSlideMovement();

	switch (EndReason)
	{
	case ESlideEndReason::Finished:
		ShooterCharacter->Crouch();
		ShooterCharacter->bCrouchHeld = true;

		SetMovementStateImmediate(EMovementState::Sit);
		break;
	case ESlideEndReason::JumpCancel:
	case ESlideEndReason::WallCancel:
	case ESlideEndReason::FallCancel:
	case ESlideEndReason::ForcedCancel:
		ShooterCharacter->UnCrouch();
		ShooterCharacter->bCrouchHeld = false;
		ShooterCharacter->bIsSprinting = false;
		SetMovementStateImmediate(
			ShooterCharacter->GetVelocity().Size2D() > KINDA_SMALL_NUMBER
				? EMovementState::Walk : EMovementState::Idle);
		break;
	}

	RefreshMovementState();
}

void UShooterMovementComponent::HandleSlideWallHit(const FHitResult& Hit)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->bIsSliding)
	{
		return;
	}

	const FVector Forward2D = ShooterCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector HitNormal2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0f);
	const float Dot = FVector::DotProduct(Forward2D, -HitNormal2D);

	if (Dot > ShooterCharacter->SlideWallStopDotValue)
	{
		StopSlide(ESlideEndReason::WallCancel);
	}
}

void UShooterMovementComponent::DoJumpStart()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerJumpStart();
	}

	if (ShooterCharacter->bIsSliding)
	{
		StopSlide(ESlideEndReason::JumpCancel);
		return;
	}

	ShooterCharacter->Jump();
	RefreshMovementState();
}

void UShooterMovementComponent::DoJumpEnd()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerJumpEnd();
	}

	ShooterCharacter->StopJumping();
}

void UShooterMovementComponent::StartSlideMovement()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->bIsSprinting = false;

	ShooterCharacter->GetWorldTimerManager().SetTimer(
		ShooterCharacter->SlideUpdateTimerHandle,
		this,
		&UShooterMovementComponent::UpdateSlideMovement,
		1.0f / 60.0f,
		true
	);
}

void UShooterMovementComponent::UpdateSlideMovement()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->bIsSliding)
	{
		return;
	}

	ShooterCharacter->SlideElapsedTime += 1.0f / 60.0f;
	const float Alpha = FMath::Clamp(ShooterCharacter->SlideElapsedTime / ShooterCharacter->SlideDuration, 0.0f, 1.0f);

	float CurveValue = 1.0f;
	if (ShooterCharacter->SlideSpeedCurve)
	{
		CurveValue = ShooterCharacter->SlideSpeedCurve->GetFloatValue(Alpha);
	}
	else if (Alpha < 0.7f)
	{
		CurveValue = FMath::Lerp(1.0f, 0.92f, Alpha / 0.7f);
	}
	else
	{
		CurveValue = FMath::Lerp(0.92f, 0.10f, (Alpha - 0.7f) / 0.3f);
	}

	const float CurrentSpeed = ShooterCharacter->SlideStartSpeed * CurveValue;
	const FVector SlideVelocity = ShooterCharacter->SlideDirection * CurrentSpeed;

	// Slide시 방향 고정
	ShooterCharacter->GetCharacterMovement()->Velocity.X = SlideVelocity.X;
	ShooterCharacter->GetCharacterMovement()->Velocity.Y = SlideVelocity.Y;
}

void UShooterMovementComponent::FinishSlideMovement()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->GetWorldTimerManager().ClearTimer(ShooterCharacter->SlideUpdateTimerHandle);
}

void UShooterMovementComponent::RefreshMovementState()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 실제 이동 컴포넌트 상태와 입력 홀드 상태를 함께 보고 애님용 이동 상태를 계산한다.
	EMovementState NewState = EMovementState::Idle;

	if (ShooterCharacter->bIsSliding)
	{
		NewState = EMovementState::Slide;
	}
	else if (!ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		NewState = EMovementState::Jump;
	}
	else if (ShooterCharacter->bIsCrouched || ShooterCharacter->bCrouchHeld)
	{
		NewState = EMovementState::Sit;
	}
	else
	{
		const float Speed2D = ShooterCharacter->GetVelocity().Size2D();
		if (Speed2D <= KINDA_SMALL_NUMBER)
		{
			NewState = EMovementState::Idle;
		}
		else if (ShooterCharacter->bIsSprinting)
		{
			NewState = EMovementState::Run;
		}
		else
		{
			NewState = EMovementState::Walk;
		}
	}

	if (ShooterCharacter->MovementState != NewState)
	{
		ShooterCharacter->MovementState = NewState;
		// 애님 인스턴스가 이 델리게이트를 바로 듣고 있으므로 상태 변경 시점에만 브로드캐스트한다.
		ShooterCharacter->OnMovementStateChanged.Broadcast(ShooterCharacter->MovementState);
	}
}

void UShooterMovementComponent::SetMovementStateImmediate(EMovementState NewState)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->MovementState == NewState)
	{
		return;
	}

	ShooterCharacter->MovementState = NewState;
	ShooterCharacter->OnMovementStateChanged.Broadcast(ShooterCharacter->MovementState);
}

void UShooterMovementComponent::StopSprintInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	ShooterCharacter->bIsSprinting = false;
	ShooterCharacter->bSprintHeld = false;

	if (!ShooterCharacter->bIsSliding)
	{
		ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = ShooterCharacter->WalkSpeed;
	}
}

bool UShooterMovementComponent::CanStartSlide() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter
		&& !ShooterCharacter->bIsDead
		&& !ShooterCharacter->bIsSliding
		&& !ShooterCharacter->GetCharacterMovement()->IsFalling()
		&& ShooterCharacter->GetCharacterMovement()->IsMovingOnGround()
		&& ShooterCharacter->GetVelocity().Size2D() >= ShooterCharacter->MinSlideSpeed
		&& !ShooterCharacter->bIsReloading
		&& !ShooterCharacter->bIsEquipping;
}
