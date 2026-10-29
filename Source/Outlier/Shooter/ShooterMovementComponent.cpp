// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
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

	DOREPLIFETIME_CONDITION(UShooterMovementComponent, bWantsToSprint, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UShooterMovementComponent, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UShooterMovementComponent, bIsSliding, COND_SkipOwner);
}

void UShooterMovementComponent::OnRep_IsSliding()
{
	// 원격은 입력/몽타주가 아니라 서버의 실제 상태를 따라 같은 emitter 반복음을 시작/종료한다.
	if (AShooterCharacter* ShooterCharacter = GetShooterCharacter())
	{
		ShooterCharacter->RefreshSlideAudio();
	}
}

void UShooterMovementComponent::HandleSprintPressed()
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (ShooterCharacter && CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
		->QueueMovementInput(EShooterMovementInput::SprintPressed)) return;
	if (!ShooterCharacter || ShooterCharacter->IsDead() || ShooterCharacter->IsSuitTransitionBlocked()
		|| !ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}

	if (ShooterCharacter->WantsToAim() || ShooterCharacter->IsAiming())
	{
		ShooterCharacter->StopAimInternal();
		ShooterCharacter->RefreshCombatState();
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
	if (ShooterCharacter && CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
		->QueueMovementInput(EShooterMovementInput::SprintReleased)) return;
	if (!ShooterCharacter)
	{
		return;
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
#if !UE_BUILD_SHIPPING
	TraceSlideDiagnostic(TEXT("CrouchOrSlideRequest"));
#endif
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (ShooterCharacter && CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
		->QueueMovementInput(EShooterMovementInput::CrouchPressed)) return;
	if (!ShooterCharacter || ShooterCharacter->IsSuitTransitionBlocked()
		|| !ShooterCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
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

	// 재장전은 유지하면서 자세만 바꾼다. Slide 진입은 CanStartSlide의 Reload 제한을 그대로 따른다.
	if (ShooterCharacter->IsActionLocked() && ShooterCharacter->GetActionLock() != EShooterActionLock::Reload)
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
	if (ShooterCharacter && CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
		->QueueMovementInput(EShooterMovementInput::CrouchReleased)) return;
	if (!ShooterCharacter || ShooterCharacter->IsSuitTransitionBlocked())
	{
		return;
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
#if !UE_BUILD_SHIPPING
	if (UShooterCharacterMovementComponent::IsSlideDiagnosticEnabled())
	{
		++SlideDiagnosticId;
		TraceSlideDiagnostic(TEXT("SlideAttempt"));
	}
#endif
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !CanStartSlide())
	{
#if !UE_BUILD_SHIPPING
		TraceSlideDiagnostic(TEXT("SlideRejected"));
#endif
		return;
	}

	auto* CharacterMovement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement());
	// 직접 TrySlide로 들어와도 임시 Slide 속도가 기본 앉기 속도로 저장되지 않게 먼저 보관한다.
	if (BaseMaxWalkSpeedCrouched < 0.0f)
	{
		BaseMaxWalkSpeedCrouched = CharacterMovement->MaxWalkSpeedCrouched;
	}
	if (!CharacterMovement->BeginSlideSimulation(this))
	{
		return;
	}

	// 이동 계산은 CMC에서 시작하고, 여기서는 기존 입력/잠금/연출을 연결한다.
	bIsSliding = true;
	bIsSlidingCanceled = false;
	ShooterCharacter->StopLean();
	ShooterCharacter->BeginActionLock(EShooterActionLock::Slide);
	SuspendSprintInternal();

	ShooterCharacter->Crouch();
	RefreshMovementState();
	CharacterMovement->ReconcileSlidePresentation();

#if !UE_BUILD_SHIPPING
	TraceSlideDiagnostic(TEXT("SlideStarted"));
#endif
}

void UShooterMovementComponent::StopSlide(ESlideEndReason EndReason)
{
	AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	if (!ShooterCharacter || !bIsSliding)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// 설정 복원 전을 남겨 낙하/충돌 중단과 정상 시간 만료를 구분한다.
	if (UShooterCharacterMovementComponent::IsSlideDiagnosticEnabled())
	{
		const FString Event = FString::Printf(TEXT("SlideStop:%s"), *UEnum::GetValueAsString(EndReason));
		TraceSlideDiagnostic(*Event);
	}
#endif
	bIsSliding = false;
	bIsSlidingCanceled = (EndReason != ESlideEndReason::Finished);
	ShooterCharacter->EndActionLock(EShooterActionLock::Slide);
	auto* CharacterMovement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement());
	CharacterMovement->SlideEndReason = uint8(EndReason);
	CharacterMovement->EndSlideSimulation();
	SuspendSprintInternal();
	CharacterMovement->ReconcileSlidePresentation();

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
	if (!ShooterCharacter || ShooterCharacter->IsSuitTransitionBlocked())
	{
		return;
	}

	if (bIsSliding)
	{
		if (CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement())
			->QueueMovementInput(EShooterMovementInput::SlideJumpCancel)) return;
		StopSlide(ESlideEndReason::JumpCancel);
		return;
	}

	const auto* JumpMovement = CastChecked<UShooterCharacterMovementComponent>(ShooterCharacter->GetCharacterMovement());
	// 바닥에서 바로 떨어진 첫 입력은 남아 있는 코요테 기회에 한해 허용한다.
	if (JumpMovement->IsFalling() && ShooterCharacter->JumpCurrentCount == 0
		&& !JumpMovement->HasCoyoteOpportunity())
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

	ShooterCharacter->StopJumping();
}

#if !UE_BUILD_SHIPPING
float UShooterMovementComponent::GetSlideDiagnosticDuration() const
{
	const AShooterCharacter* Shooter = GetShooterCharacter();
	return Shooter ? Shooter->SlideDuration : 0.0f;
}

void UShooterMovementComponent::TraceSlideDiagnostic(const TCHAR* Event) const
{
	if (!UShooterCharacterMovementComponent::IsSlideDiagnosticEnabled())
	{
		return;
	}
	AShooterCharacter* Shooter = GetShooterCharacter();
	auto* Movement = Shooter ? Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement()) : nullptr;
	if (Movement)
	{
		Movement->LogSlideDiagnostic(Event);
	}
}
#endif

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
	UCharacterMovementComponent* CharacterMovement = ShooterCharacter->GetCharacterMovement();
	const float StealthSpeedMultiplier = GetStealthSpeedMultiplier();
	CharacterMovement->MaxWalkSpeed = (bCanUseSprintSpeed
		? ShooterCharacter->SprintSpeed : ShooterCharacter->WalkSpeed) * StealthSpeedMultiplier;
	if (BaseMaxWalkSpeedCrouched < 0.0f)
	{
		BaseMaxWalkSpeedCrouched = CharacterMovement->MaxWalkSpeedCrouched;
	}
	// 항상 원래 속도에서 계산해 반복 갱신이나 은신/종료 버프 중첩으로 배율이 누적되지 않게 한다.
	// 슬라이드 중의 앉기 속도는 기존 감속 커브가 소유한다.
	if (!bIsSliding)
	{
		CharacterMovement->MaxWalkSpeedCrouched = BaseMaxWalkSpeedCrouched * StealthSpeedMultiplier;
	}
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
	if (AShooterCharacter* Shooter = GetShooterCharacter())
	{
		auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
		// 조준/공격 등 기존 정책으로 해제한 의도도 다음 이동에 반영한다. 과거 재생 입력은 실제 키 상태에 쓰지 않는다.
		if (!Movement->bApplyingMovementInput && !Movement->IsReplayingMovement())
		{
			Movement->LiveMovementInput.bSprint = false;
		}
	}
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
		ShooterCharacter->GetCharacterMovement()->MaxWalkSpeed = ShooterCharacter->WalkSpeed * GetStealthSpeedMultiplier();
	}
}

void UShooterMovementComponent::ClearInputIntent()
{
	bWantsToSprint = false;
	bWantsToCrouch = false;
	if (AShooterCharacter* Shooter = GetShooterCharacter())
	{
		auto* Movement = CastChecked<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement());
		if (!Movement->bApplyingMovementInput && !Movement->IsReplayingMovement())
		{
			Movement->LiveMovementInput.bSprint = false;
			Movement->LiveMovementInput.bCrouch = false;
		}
	}
}

bool UShooterMovementComponent::CanStartSlide() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	return ShooterCharacter
		&& !ShooterCharacter->IsDead()
		&& !ShooterCharacter->IsSuitTransitionBlocked()
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
		&& !ShooterCharacter->IsSuitTransitionBlocked()
		&& !ShooterCharacter->IsDead() && !ShooterCharacter->GetCharacterMovement()->IsCrouching()
		&& !bWantsToCrouch && !bIsSliding
		&& !ShooterCharacter->WantsToAim()
		&& !ShooterCharacter->IsReloading()
		&& !ShooterCharacter->IsActionLocked();
}

float UShooterMovementComponent::GetStealthSpeedMultiplier() const
{
	const AShooterCharacter* ShooterCharacter = GetShooterCharacter();
	const UOutlierAbilitySystemComponent* AbilitySystem = ShooterCharacter
		? ShooterCharacter->GetOutlierAbilitySystemComponent() : nullptr;
	return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::ShooterStealthSpeedBoost())
		? ShooterCharacter->GetStealthMovementSpeedMultiplier() : 1.0f;
}
