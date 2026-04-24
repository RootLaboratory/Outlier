// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Curves/CurveFloat.h"
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
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerSetSprintState(true);
	}

	if (ShooterCharacter->bIsDead || ShooterCharacter->GetCharacterMovement()->IsCrouching()
		|| ShooterCharacter->WantsToAim()
		|| ShooterCharacter->IsReloading())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s HandleSprintPressed blocked Dead=%d Crouching=%d WantsAim=%d Reloading=%d MoveState=%d CombatState=%d"),
			OutlierNet::GetNetPrefix(ShooterCharacter),
			*ShooterCharacter->GetName(),
			ShooterCharacter->bIsDead ? 1 : 0,
			ShooterCharacter->GetCharacterMovement()->IsCrouching() ? 1 : 0,
			ShooterCharacter->WantsToAim() ? 1 : 0,
			ShooterCharacter->IsReloading() ? 1 : 0,
			static_cast<int32>(ShooterCharacter->MovementState),
			static_cast<int32>(ShooterCharacter->CombatState)
		);
		return;
	}

	bWantsToSprint = true;
	bIsSprinting = true;
	ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = ShooterCharacter->SprintSpeed;
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

void UShooterMovementComponent::HandleCrouchToggled()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	// 토글 입력은 crouch 의도만 바꾸고, 실제 crouch 상태는 ACharacter가 유지
	if (ShooterCharacter->bIsCrouched || bWantsToCrouch)
	{
		RequestUncrouch();
		return;
	}

	RequestCrouchOrSlide();
}

void UShooterMovementComponent::RequestCrouchOrSlide()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	if (!ShooterCharacter->HasAuthority())
	{
		ShooterCharacter->ServerRequestCrouchOrSlide();
	}

	if (ShooterCharacter->bIsDead)
	{
		return;
	}

	bWantsToCrouch = true;

	if (bIsSprinting && ShooterCharacter->GetVelocity().SizeSquared() > 0.0f)
	{
		TrySlide();
		return;
	}

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
				: (bIsSprinting ? EMovementState::Run : EMovementState::Walk));
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

	// 슬라이드는 이동 상태, 웅크림, 타이머가 함께 바뀌는 복합 액션
	// 슬라이드는 sprint와 crouch 상태를 잠시 덮어쓰는 복합 액션
	bIsSliding = true;
	SlideDirection = Velocity2D;
	SlideStartSpeed = ShooterCharacter->SprintSpeed * ShooterCharacter->SlideSpeedMultiplier;
	SlideElapsedTime = 0.0f;

	ShooterCharacter->Crouch();
	RefreshMovementState();
	ShooterCharacter->PlayAnimMontage(ShooterCharacter->SlideMontage);

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
	ShooterCharacter->GetWorldTimerManager().ClearTimer(SlideTimerHandle);
	FinishSlideMovement();

	switch (EndReason)
	{
	case ESlideEndReason::Finished:
		// 슬라이드가 정상 종료되면 crouch 의도를 유지해서 다음 토글에 자연스럽게 일어남
		ShooterCharacter->Crouch();
		bWantsToCrouch = true;

		SetMovementStateImmediate(EMovementState::Crouch);
		break;
	case ESlideEndReason::JumpCancel:
	case ESlideEndReason::WallCancel:
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

	bIsSprinting = false;

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

	SlideElapsedTime += 1.0f / 60.0f;
	const float Alpha = FMath::Clamp(SlideElapsedTime / ShooterCharacter->SlideDuration, 0.0f, 1.0f);

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

	const float CurrentSpeed = SlideStartSpeed * CurveValue;
	const FVector SlideVelocity = SlideDirection * CurrentSpeed;

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

	if (bIsSliding)
	{
		NewState = EMovementState::Slide;
	}
	else if (!ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
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
			UE_LOG(LogTemp, Error, TEXT("Idle Now"));
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
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter)
	{
		return;
	}

	bIsSprinting = false;
	bWantsToSprint = false;

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
		&& !ShooterCharacter->bIsDead
		&& !bIsSliding
		&& !ShooterCharacter->GetCharacterMovement()->IsFalling()
		&& ShooterCharacter->GetCharacterMovement()->IsMovingOnGround()
		&& ShooterCharacter->GetVelocity().Size2D() >= ShooterCharacter->MinSlideSpeed
		&& !ShooterCharacter->IsReloading()
		&& !ShooterCharacter->bIsEquipping;
}
