// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"

UShooterMovementComponent::UShooterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShooterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UShooterMovementComponent, bWantsToSprint);
	DOREPLIFETIME(UShooterMovementComponent, bIsSprinting);
	DOREPLIFETIME(UShooterMovementComponent, bIsSliding);
}

void UShooterMovementComponent::HandleSprintPressed()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDead()
		|| !ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}

	if (ShooterCharacter->WantsToAim() || ShooterCharacter->IsAiming())
	{
		ShooterCharacter->StopAimInternal();
		ShooterCharacter->RefreshCombatState();
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetSprintState(true);
	}

	bWantsToSprint = true;
	if (CanSprint())
	{
		ShooterCharacter->TryStopAttack();
		ShooterCharacter->StopLean();
	}
	RefreshMovementState();
}

void UShooterMovementComponent::HandleSprintReleased()
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

	bWantsToSprint = false;
	StopSprintInternal();
	RefreshMovementState();
}

void UShooterMovementComponent::HandleCrouchPressed()
{
	RequestCrouchOrSlide();
}

void UShooterMovementComponent::HandleCrouchReleased()
{
	RequestUncrouch();
}

void UShooterMovementComponent::RequestCrouchOrSlide()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerRequestCrouchOrSlide();
	}

	if (ShooterCharacter->IsDead())
	{
		return;
	}

	// 슬라이드 중 다시 누르면 종료 후 유지할 앉기 의도만 변경
	if (bIsSliding)
	{
		bWantsToCrouch = true;
		return;
	}

	if (ShooterCharacter->IsActionLocked())
	{
		return;
	}

	bWantsToCrouch = true;

	if (bIsSprinting && ShooterCharacter->GetVelocity().SizeSquared() > 0.0f)
	{
		TrySlide();
		if (bIsSliding)
		{
			return;
		}
	}

	SuspendSprintInternal();
	ShooterCharacter->Crouch();
	SetMovementStateImmediate(EMovementState::Crouch);
}

void UShooterMovementComponent::RequestUncrouch()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerRequestUncrouch();
	}

	bWantsToCrouch = false;

	if (bIsSliding)
	{
		return;
	}

	ShooterCharacter->UnCrouch();

	RefreshMovementState();
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

	// 슬라이드는 이동 상태, 웅크림, 타이머가 함께 바뀌는 복합 액션
	// 슬라이드는 sprint와 crouch 상태를 잠시 덮어쓰는 복합 액션
	bIsSliding = true;
	bIsSlidingCanceled = false;
	ShooterCharacter->StopLean();
	ShooterCharacter->BeginActionLock(EShooterActionLock::Slide);
	SlideDirection = Velocity2D;
	SlideStartSpeed = ShooterCharacter->GetVelocity().Size2D() * FMath::Max(0.0f, ShooterCharacter->SlideSpeedMultiplier);
	CurrentSlideSpeed = SlideStartSpeed;
	SlideElapsedTime = 0.0f;
	SlideStartTime = ShooterCharacter->GetWorld()->GetTimeSeconds();

	ShooterCharacter->Crouch();
	RefreshMovementState();
	ShooterCharacter->MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Slide, ShooterCharacter->GetWeaponType());
	if (ShooterCharacter->IsLocallyControlled())
	{
		ShooterCharacter->PlayFirstPersonActionMontage(EShooterMontageAction::Slide, ShooterCharacter->GetWeaponType());
	}

	StartSlideMovement();

	FTimerDelegate SlideEndDelegate;
	SlideEndDelegate.BindUObject(ShooterCharacter, &AShooterCharacter::StopSlide, ESlideEndReason::Finished);

	ShooterCharacter->GetWorldTimerManager().SetTimer(
		SlideTimerHandle,
		SlideEndDelegate,
		ShooterCharacter->SlideDuration,
		false);
}

void UShooterMovementComponent::StopSlide(ESlideEndReason EndReason)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !bIsSliding)
	{
		return;
	}

	bIsSliding = false;
	bIsSlidingCanceled = (EndReason != ESlideEndReason::Finished);
	ShooterCharacter->EndActionLock(EShooterActionLock::Slide);
	ShooterCharacter->GetWorldTimerManager().ClearTimer(SlideTimerHandle);
	FinishSlideMovement();
	SuspendSprintInternal();
	ShooterCharacter->StopSplitMontages(
		ShooterCharacter->GetActionMontage(EShooterMontageAction::Slide, true),
		ShooterCharacter->GetActionMontage(EShooterMontageAction::Slide, false));

	switch (EndReason)
	{
	case ESlideEndReason::Finished:
	case ESlideEndReason::WallCancel:
		if (bWantsToCrouch)
		{
			ShooterCharacter->Crouch();
		}
		else
		{
			ShooterCharacter->UnCrouch();
		}
		break;
	case ESlideEndReason::JumpCancel:
	case ESlideEndReason::FallCancel:
	case ESlideEndReason::ForcedCancel:
		ShooterCharacter->UnCrouch();
		bWantsToCrouch = false;
		bIsSprinting = false;
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
	if (!ShooterCharacter || !bIsSliding)
	{
		return;
	}

	const FVector Forward2D = ShooterCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector HitNormal2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0f);
	const float Dot = FVector::DotProduct(Forward2D, -HitNormal2D);

	if (Dot > ShooterCharacter->SlideWallStopDotThreshold)
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

	if (bIsSliding)
	{
		StopSlide(ESlideEndReason::JumpCancel);
		return;
	}

	// 엔진은 낙하 중 첫 점프도 허용하므로, 지상 점프 없이 떨어진 경우에는 입력을 넘기지 않는다.
	if (ShooterCharacter->GetCharacterMovement()->IsFalling() && ShooterCharacter->JumpCurrentCount == 0)
	{
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

	SuspendSprintInternal();
	UCharacterMovementComponent* CharacterMovement = ShooterCharacter->GetCharacterMovement();
	SavedGroundFriction = CharacterMovement->GroundFriction;
	SavedBrakingDecelerationWalking = CharacterMovement->BrakingDecelerationWalking;
	SavedBrakingFrictionFactor = CharacterMovement->BrakingFrictionFactor;
	SavedMaxAcceleration = CharacterMovement->MaxAcceleration;
	SavedMaxWalkSpeedCrouched = CharacterMovement->MaxWalkSpeedCrouched;
	// 슬라이드 감속은 커브로 처리하므로 기본 이동의 마찰, 제동, 가속은 잠시 해제
	CharacterMovement->GroundFriction = 0.0f;
	CharacterMovement->BrakingDecelerationWalking = 0.0f;
	CharacterMovement->BrakingFrictionFactor = 0.0f;
	CharacterMovement->MaxAcceleration = 0.0f;
	UpdateSlideMovement();

	ShooterCharacter->GetWorldTimerManager().SetTimer(
		SlideUpdateTimerHandle,
		this,
		&UShooterMovementComponent::UpdateSlideMovement,
		1.0f / 60.0f,
		true
	);
}

void UShooterMovementComponent::UpdateSlideMovement()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !bIsSliding)
	{
		return;
	}

	SlideElapsedTime = static_cast<float>(ShooterCharacter->GetWorld()->GetTimeSeconds() - SlideStartTime);
	const float Alpha = FMath::Clamp(SlideElapsedTime / FMath::Max(ShooterCharacter->SlideDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	float CurveValue = 1.0f;
	if (ShooterCharacter->SlideSpeedCurve)
	{
		CurveValue = ShooterCharacter->SlideSpeedCurve->GetFloatValue(Alpha);
		// Cubic 보간으로 마지막 속도 비율보다 낮아졌다가 다시 올라가는 현상 방지
		CurveValue = FMath::Max(CurveValue, ShooterCharacter->SlideSpeedCurve->GetFloatValue(1.0f));
	}
	else if (Alpha < 0.7f)
	{
		CurveValue = FMath::Lerp(1.0f, 0.92f, Alpha / 0.7f);
	}
	else
	{
		CurveValue = FMath::Lerp(0.92f, 0.10f, (Alpha - 0.7f) / 0.3f);
	}

	// 커브 Y는 매번 곱하는 감속값이 아니라 슬라이드 시작 속도 대비 남은 비율
	const float TargetSpeed = SlideStartSpeed * FMath::Clamp(CurveValue, 0.0f, 1.0f);
	CurrentSlideSpeed = FMath::Min(CurrentSlideSpeed, TargetSpeed);
	const FVector SlideVelocity = SlideDirection * CurrentSlideSpeed;
	ShooterCharacter->GetCharacterMovement()->MaxWalkSpeedCrouched = CurrentSlideSpeed;

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

	ShooterCharacter->GetWorldTimerManager().ClearTimer(SlideUpdateTimerHandle);
	UCharacterMovementComponent* CharacterMovement = ShooterCharacter->GetCharacterMovement();
	CharacterMovement->GroundFriction = SavedGroundFriction;
	CharacterMovement->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
	CharacterMovement->BrakingFrictionFactor = SavedBrakingFrictionFactor;
	CharacterMovement->MaxAcceleration = SavedMaxAcceleration;
	CharacterMovement->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
}

void UShooterMovementComponent::RefreshMovementState()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 실제 이동 컴포넌트 상태와 입력 홀드 상태를 함께 보고 애님용 이동 상태를 계산
	// MovementComponent가 sprint와 slide 내부 상태를 관리하고, 그 결과로 복제되는 이동 enum을 계산
	EMovementState NewState = EMovementState::Idle;
	const bool bCanUseSprintSpeed = bWantsToSprint && CanSprint();
	const bool bWasSprinting = bIsSprinting;
	bIsSprinting = bCanUseSprintSpeed && ShooterCharacter->GetVelocity().Size2D() > KINDA_SMALL_NUMBER;
	ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = bCanUseSprintSpeed
		? ShooterCharacter->SprintSpeed : ShooterCharacter->WalkSpeed;
	if (bIsSprinting && !bWasSprinting
		&& (ShooterCharacter->HasAuthority() || ShooterCharacter->IsLocallyControlled()))
	{
		ShooterCharacter->TryStopAttack();
		ShooterCharacter->StopLean();
	}

	if (bIsSliding)
	{
		NewState = EMovementState::Slide;
	}
	else if (ShooterCharacter->GetCharacterMovement()->IsFalling())
	{
		NewState = EMovementState::Jump;
	}
	else if (ShooterCharacter->bIsCrouched || bWantsToCrouch)
	{
		NewState = EMovementState::Crouch;
	}
	else
	{
		const float Speed2D = ShooterCharacter->GetVelocity().Size2D();

		if (Speed2D <= KINDA_SMALL_NUMBER)
		{
			NewState = EMovementState::Idle;
		}
		else if (bIsSprinting)
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
		// 복제 enum은 Character가 들고 있으므로 상태가 바뀌는 시점에만 브로드캐스트
		// 애님 인스턴스가 이 델리게이트를 바로 듣고 있으므로 상태 변경 시점에만 브로드캐스트
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
	bWantsToSprint = false;
	SuspendSprintInternal();
}

void UShooterMovementComponent::SuspendSprintInternal()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	bIsSprinting = false;

	if (!bIsSliding)
	{
		ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = ShooterCharacter->WalkSpeed;
	}
}

void UShooterMovementComponent::ClearInputIntent()
{
	bWantsToSprint = false;
	bWantsToCrouch = false;
}

bool UShooterMovementComponent::CanStartSlide() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter
		&& !ShooterCharacter->IsDead()
		&& !bIsSliding
		&& ShooterCharacter->SlideDuration > KINDA_SMALL_NUMBER
		&& !ShooterCharacter->GetCharacterMovement()->IsFalling()
		&& ShooterCharacter->GetCharacterMovement()->IsMovingOnGround()
		&& ShooterCharacter->GetVelocity().Size2D() >= ShooterCharacter->MinSlideSpeed
		&& !ShooterCharacter->IsReloading()
		&& !ShooterCharacter->IsActionLocked();
}

bool UShooterMovementComponent::CanSprint() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();

	return ShooterCharacter && ShooterCharacter->GetCharacterMovement()->IsMovingOnGround()
		&& !ShooterCharacter->IsDead() && !ShooterCharacter->GetCharacterMovement()->IsCrouching()
		&& !bWantsToCrouch && !bIsSliding
		&& !ShooterCharacter->WantsToAim()
		&& !ShooterCharacter->IsReloading()
		&& !ShooterCharacter->IsActionLocked();
}
