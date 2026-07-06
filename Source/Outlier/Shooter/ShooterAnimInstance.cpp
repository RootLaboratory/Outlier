// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"

namespace
{
	bool IsFirearmWeapon(EWeaponType WeaponType)
	{
		return WeaponType == EWeaponType::Rifle || WeaponType == EWeaponType::Pistol;
	}

	float InterpAnimAlpha(float Current, float Target, float DeltaSeconds, float BlendInSpeed, float BlendOutSpeed)
	{
		const float InterpSpeed = Target > Current ? BlendInSpeed : BlendOutSpeed;
		return FMath::FInterpTo(Current, Target, DeltaSeconds, InterpSpeed);
	}

	// FInterpTo는 0에 점근만 하고 도달하지 못한다. 마지막 미세한 기어감을
	// 잘라내서 벽 오프셋이 실제로 0으로 끝나게 한다
	float SnapWallAlpha(float Alpha, float Target)
	{
		return (Target <= KINDA_SMALL_NUMBER && Alpha < 0.04f) ? 0.0f : Alpha;
	}

	FRotator ScaleRotator(const FRotator& Rotator, float Scale)
	{
		return FRotator(Rotator.Pitch * Scale, Rotator.Yaw * Scale, Rotator.Roll * Scale);
	}
}

void UShooterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (CachedShooterCharacter)
	{
		CachedShooterCharacter->OnCharacterDeath.RemoveDynamic(this, &UShooterAnimInstance::HandleOwnerDeath);
		CachedShooterCharacter->OnMovementStateChanged.RemoveDynamic(this, &UShooterAnimInstance::HandleOwnerMovementStateChanged);
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	CachedShooterCharacter = Cast<AShooterCharacter>(OwnerPawn);

	if (!CachedShooterCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TPAnim] NativeInitializeAnimation failed OwnerPawn=%s"), *GetNameSafe(OwnerPawn));
		return;
	}

	if (USkeletalMeshComponent* OwningMesh = GetOwningComponent())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s [TPAnim] Init AnimClass=%s Mesh=%s Owner=%s WeaponType=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			*GetClass()->GetName(),
			*GetNameSafe(OwningMesh),
			*GetNameSafe(CachedShooterCharacter),
			static_cast<int32>(CachedShooterCharacter->GetWeaponType()));
	}

	CachedShooterCharacter->OnCharacterDeath.AddUniqueDynamic(this, &UShooterAnimInstance::HandleOwnerDeath);
	CachedShooterCharacter->OnMovementStateChanged.AddUniqueDynamic(this, &UShooterAnimInstance::HandleOwnerMovementStateChanged);

	MovementState = CachedShooterCharacter->GetMovementState();
	CombatState = CachedShooterCharacter->GetCombatState();
	WeaponMode = CachedShooterCharacter->GetWeaponMode();
	bIsSliding    = CachedShooterCharacter->IsSliding();
	bIsAiming     = CachedShooterCharacter->IsAiming();
	bIsReloading  = CachedShooterCharacter->IsReloading();
	bIsDead		  = CachedShooterCharacter->IsDead();
}

void UShooterAnimInstance::NativeUninitializeAnimation()
{
	if (CachedShooterCharacter)
	{
		CachedShooterCharacter->OnCharacterDeath.RemoveDynamic(this, &UShooterAnimInstance::HandleOwnerDeath);
		CachedShooterCharacter->OnMovementStateChanged.RemoveDynamic(this, &UShooterAnimInstance::HandleOwnerMovementStateChanged);
		CachedShooterCharacter = nullptr;
	}

	Super::NativeUninitializeAnimation();
}

void UShooterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!CachedShooterCharacter)
	{
		APawn* OwnerPawn = TryGetPawnOwner();
		CachedShooterCharacter = Cast<AShooterCharacter>(OwnerPawn);
	}

	if (!CachedShooterCharacter)
	{
		Speed = 0.0f;
		Direction = 0.0f;
		CurrentWeaponType = EWeaponType::Unarmed;
		AimYaw = 0.0f;
		AimPitch = 0.0f;
		LeanAlpha = 0.0f;
		bIsCrouching = false;
		bIsSprinting = false;
		bIsSliding = false;
		bIsSlidingCanceled = false;
		bIsGrounded = true;
		bIsInAir = false;
		bIsAiming = false;
		bIsReloading = false;
		bIsPrimaryWeapon = false;
		bIsSecondaryWeapon = false;
		ResetThirdPersonProceduralState();
		return;
	}

	UCharacterMovementComponent* CharacterMovement = CachedShooterCharacter->GetCharacterMovement();
	if (!CharacterMovement)
	{
		Speed = 0.0f;
		Direction = 0.0f;
		bIsGrounded = true;
		bIsInAir = false;
		ResetThirdPersonProceduralState();
		return;
	}

	Speed		      = CharacterMovement->Velocity.Size2D();
	Direction		  = UKismetAnimationLibrary::CalculateDirection(
		CharacterMovement->Velocity,
		CachedShooterCharacter->GetActorRotation()
	);
	CurrentWeaponType = CachedShooterCharacter->GetWeaponType();
	LeanAlpha     = CachedShooterCharacter->GetCurrentLeanAlpha();
	MovementState = CachedShooterCharacter->GetMovementState();
	CombatState   = CachedShooterCharacter->GetCombatState();
	WeaponMode    = CachedShooterCharacter->GetWeaponMode();

	bIsCrouching	   = CachedShooterCharacter->bIsCrouched;
	bIsSprinting	   = CachedShooterCharacter->IsSprinting();
	bIsSliding		   = CachedShooterCharacter->IsSliding();
	bIsSlidingCanceled = CachedShooterCharacter->IsSlidingCanceled();
	bIsInAir		   = CharacterMovement->IsFalling();
	bIsGrounded		   = !bIsInAir && CharacterMovement->IsMovingOnGround();
	bIsAiming	       = CachedShooterCharacter->IsAiming();
	bIsReloading       = CachedShooterCharacter->IsReloading();
	bIsDead		       = CachedShooterCharacter->IsDead();
	bIsPrimaryWeapon   = (WeaponMode == EWeaponMode::Primary);
	bIsSecondaryWeapon = (WeaponMode == EWeaponMode::Secondary);

	AimYaw = CachedShooterCharacter->GetAimYawForAnimation();
	AimPitch = CachedShooterCharacter->GetAimPitchForAnimation();

	AWeaponBase* CurrentWeapon = CachedShooterCharacter->GetCurrentWeapon();

	UpdateThirdPersonProceduralState(DeltaSeconds, CurrentWeapon);
}

void UShooterAnimInstance::HandleOwnerDeath()
{
	bIsDead = true;
}

void UShooterAnimInstance::HandleOwnerMovementStateChanged(EMovementState NewState)
{
	MovementState = NewState;
}

void UShooterAnimInstance::AddThirdPersonRecoil(float GameplayRecoilScale, FVector2D NormalizedShotDirection)
{
	const FWeaponValues* WeaponValues = nullptr;
	if (CachedShooterCharacter)
	{
		if (const AWeaponBase* CurrentWeapon = CachedShooterCharacter->GetCurrentWeapon())
		{
			if (const UProceduralAnimValues* ProceduralValues = CurrentWeapon->GetFirstPersonProceduralValues())
			{
				WeaponValues = &ProceduralValues->WeaponValues;
			}
		}
	}

	const FVector RecoilHandLocScale = WeaponValues
		? WeaponValues->ThirdPersonRecoilHandLocScale
		: ThirdPersonRecoilHandLocScale;
	const FRotator RecoilHandRotScale = WeaponValues
		? WeaponValues->ThirdPersonRecoilHandRotScale
		: ThirdPersonRecoilHandRotScale;
	const float RecoilImpulseScale = WeaponValues
		? WeaponValues->ThirdPersonRecoilImpulseScale
		: ThirdPersonRecoilImpulseScale;
	const float ImpulseScale = FMath::Max(GameplayRecoilScale, 0.0f) * FMath::Max(RecoilImpulseScale, 0.0f);
	const float HorizontalScale = FMath::Clamp(NormalizedShotDirection.X, -1.0f, 1.0f);

	ThirdPersonRecoilLocTarget += RecoilHandLocScale * ImpulseScale;
	ThirdPersonRecoilRotTarget.Pitch += RecoilHandRotScale.Pitch * ImpulseScale;
	ThirdPersonRecoilRotTarget.Yaw += RecoilHandRotScale.Yaw * HorizontalScale * ImpulseScale;
	ThirdPersonRecoilRotTarget.Roll += RecoilHandRotScale.Roll * HorizontalScale * ImpulseScale;
}

void UShooterAnimInstance::ResetThirdPersonProceduralState()
{
	TurnInPlaceYaw = 0.0f;
	TurnInPlaceDirection = 0.0f;
	TurnInPlaceVisualYaw = 0.0f;
	PreviousBaseAimYaw = 0.0f;
	TurnInPlaceResetTimeRemaining = 0.0f;
	bHasPreviousBaseAimYaw = false;
	bIsTurnInPlaceConsuming = false;
	bTurnInPlaceResetRequested = false;
	ThirdPersonRecoilLocTarget = FVector::ZeroVector;
	ThirdPersonRecoilRotTarget = FRotator::ZeroRotator;

	ThirdPersonADSAlpha = 0.0f;
	ThirdPersonSprintPoseAlpha = 0.0f;
	ThirdPersonLeanLeftAlpha = 0.0f;
	ThirdPersonLeanRightAlpha = 0.0f;
	ThirdPersonADSLeanLeftAlpha = 0.0f;
	ThirdPersonADSLeanRightAlpha = 0.0f;
	ThirdPersonWallAvoidUpPoseAlpha = 0.0f;
	ThirdPersonWallAvoidDownPoseAlpha = 0.0f;
	ThirdPersonWallAimBreakAlpha = 0.0f;
	ThirdPersonWallTightAlpha = 0.0f;
	ThirdPersonWallTightADSAlpha = 0.0f;
	ThirdPersonWallAvoidUpHipPoseAlpha = 0.0f;
	ThirdPersonWallAvoidUpADSPoseAlpha = 0.0f;
	ThirdPersonWallAvoidDownHipPoseAlpha = 0.0f;
	ThirdPersonWallAvoidDownADSPoseAlpha = 0.0f;
	ThirdPersonAimAlpha = 0.0f;
	ThirdPersonAimOffsetAlpha = 0.0f;
	ThirdPersonHipAimOffsetAlpha = 0.0f;
	ThirdPersonADSAimOffsetAlpha = 0.0f;
	ThirdPersonUpperBodyAlpha = 0.0f;
	ThirdPersonFireAlpha = 0.0f;
	ThirdPersonReloadAlpha = 0.0f;
	ThirdPersonSlideAlpha = 0.0f;
	ThirdPersonTurnInPlaceAlpha = 0.0f;
	ThirdPersonTurnInPlacePlayRate = 1.0f;
	ThirdPersonRecoilLoc = FVector::ZeroVector;
	ThirdPersonRecoilRot = FRotator::ZeroRotator;
	ThirdPersonRecoilAlpha = 0.0f;
	ThirdPersonRecoilSpineLoc = FVector::ZeroVector;
	ThirdPersonRecoilSpineRot = FRotator::ZeroRotator;
	ThirdPersonWallOffsetLoc = FVector::ZeroVector;
	ThirdPersonWallOffsetRot = FRotator::ZeroRotator;
	ThirdPersonWallOffsetAlpha = 0.0f;
	ThirdPersonWallMuzzleBlockAlpha = 0.0f;
	ThirdPersonWallVeryCloseAlpha = 0.0f;
	ThirdPersonWallHardStopAlpha = 0.0f;
}

float UShooterAnimInstance::UpdateTurnInPlaceYaw(float DeltaSeconds, bool bCanTurnInPlace)
{
	bTurnInPlaceResetRequested = false;

	if (!CachedShooterCharacter)
	{
		bHasPreviousBaseAimYaw = false;
		TurnInPlaceVisualYaw = 0.0f;
		TurnInPlaceResetTimeRemaining = 0.0f;
		bIsTurnInPlaceConsuming = false;
		return 0.0f;
	}

	const float CurrentBaseAimYaw = CachedShooterCharacter->GetBaseAimRotation().Yaw;
	if (!bHasPreviousBaseAimYaw)
	{
		PreviousBaseAimYaw = CurrentBaseAimYaw;
		bHasPreviousBaseAimYaw = true;
		return TurnInPlaceVisualYaw;
	}

	const float AimYawDelta = FRotator::NormalizeAxis(CurrentBaseAimYaw - PreviousBaseAimYaw);
	PreviousBaseAimYaw = CurrentBaseAimYaw;

	if (!bCanTurnInPlace)
	{
		TurnInPlaceVisualYaw = FMath::FInterpTo(TurnInPlaceVisualYaw, 0.0f, DeltaSeconds, ThirdPersonTurnInPlaceBlendOutSpeed);
		TurnInPlaceResetTimeRemaining = 0.0f;
		bIsTurnInPlaceConsuming = false;
		return TurnInPlaceVisualYaw;
	}

	if (TurnInPlaceResetTimeRemaining > 0.0f)
	{
		TurnInPlaceResetTimeRemaining = FMath::Max(0.0f, TurnInPlaceResetTimeRemaining - DeltaSeconds);
		TurnInPlaceVisualYaw = 0.0f;
		bTurnInPlaceResetRequested = true;
		return 0.0f;
	}

	TurnInPlaceVisualYaw = FRotator::NormalizeAxis(TurnInPlaceVisualYaw + AimYawDelta);
	TurnInPlaceVisualYaw = FMath::Clamp(TurnInPlaceVisualYaw, -ThirdPersonTurnInPlaceFullYaw, ThirdPersonTurnInPlaceFullYaw);

	if (FMath::Abs(TurnInPlaceVisualYaw) >= ThirdPersonTurnInPlaceStartYaw)
	{
		bIsTurnInPlaceConsuming = true;
	}

	if (bIsTurnInPlaceConsuming)
	{
		const float ConsumeDirection = TurnInPlaceVisualYaw > 0.0f ? -1.0f : 1.0f;
		const float ConsumeAmount = ThirdPersonTurnInPlaceYawConsumeSpeed * DeltaSeconds;
		const float NewVisualYaw = TurnInPlaceVisualYaw + (ConsumeDirection * ConsumeAmount);

		if (FMath::Sign(TurnInPlaceVisualYaw) != FMath::Sign(NewVisualYaw))
		{
			TurnInPlaceVisualYaw = 0.0f;
			bIsTurnInPlaceConsuming = false;
			TurnInPlaceResetTimeRemaining = ThirdPersonTurnInPlaceResetHoldTime;
			bTurnInPlaceResetRequested = true;
		}
		else
		{
			TurnInPlaceVisualYaw = NewVisualYaw;
		}
	}

	return TurnInPlaceVisualYaw;
}

void UShooterAnimInstance::UpdateThirdPersonLean(float DeltaSeconds)
{
	const float SignedLeanAlpha = FMath::Clamp(LeanAlpha, -1.0f, 1.0f);
	const float ADSAlpha = FMath::Clamp(ThirdPersonADSAlpha, 0.0f, 1.0f);
	const float HipLeanScale = 1.0f - ADSAlpha;
	const float LeanLeftTargetAlpha = FMath::Max(-SignedLeanAlpha, 0.0f) * HipLeanScale;
	const float LeanRightTargetAlpha = FMath::Max(SignedLeanAlpha, 0.0f) * HipLeanScale;
	const float ADSLeanLeftTargetAlpha = FMath::Max(-SignedLeanAlpha, 0.0f) * ADSAlpha;
	const float ADSLeanRightTargetAlpha = FMath::Max(SignedLeanAlpha, 0.0f) * ADSAlpha;

	ThirdPersonLeanLeftAlpha = FMath::FInterpTo(
		ThirdPersonLeanLeftAlpha,
		LeanLeftTargetAlpha,
		DeltaSeconds,
		ThirdPersonLeanInterpSpeed
	);
	ThirdPersonLeanRightAlpha = FMath::FInterpTo(
		ThirdPersonLeanRightAlpha,
		LeanRightTargetAlpha,
		DeltaSeconds,
		ThirdPersonLeanInterpSpeed
	);
	ThirdPersonADSLeanLeftAlpha = FMath::FInterpTo(
		ThirdPersonADSLeanLeftAlpha,
		ADSLeanLeftTargetAlpha,
		DeltaSeconds,
		ThirdPersonLeanInterpSpeed
	);
	ThirdPersonADSLeanRightAlpha = FMath::FInterpTo(
		ThirdPersonADSLeanRightAlpha,
		ADSLeanRightTargetAlpha,
		DeltaSeconds,
		ThirdPersonLeanInterpSpeed
	);
}

float UShooterAnimInstance::GetThirdPersonSprintPoseTargetAlpha(const FWeaponValues* WeaponValues) const
{
	const float AdditiveMultiplier = WeaponValues ? WeaponValues->ThirdPersonSprintMultiplier : 1.0f;
	return FMath::Clamp(FMath::Max(AdditiveMultiplier, 0.0f), 0.0f, 1.0f);
}

void UShooterAnimInstance::UpdateThirdPersonRecoil(float DeltaSeconds, const FWeaponValues* WeaponValues)
{
	const FVector RecoilHandLocScale = WeaponValues ? WeaponValues->ThirdPersonRecoilHandLocScale : ThirdPersonRecoilHandLocScale;
	const FVector RecoilSpineLocScale = WeaponValues ? WeaponValues->ThirdPersonRecoilSpineLocScale : ThirdPersonRecoilSpineLocScale;
	const FRotator RecoilSpineRotScale = WeaponValues ? WeaponValues->ThirdPersonRecoilSpineRotScale : ThirdPersonRecoilSpineRotScale;
	const float RecoilRecoverySpeed = WeaponValues ? WeaponValues->ThirdPersonRecoilRecoverySpeed : ThirdPersonRecoilRecoverySpeed;
	ThirdPersonRecoilLocTarget = FMath::VInterpTo(
		ThirdPersonRecoilLocTarget,
		FVector::ZeroVector,
		DeltaSeconds,
		RecoilRecoverySpeed
	);
	ThirdPersonRecoilRotTarget = FMath::RInterpTo(
		ThirdPersonRecoilRotTarget,
		FRotator::ZeroRotator,
		DeltaSeconds,
		RecoilRecoverySpeed
	);

	const float RecoilMagnitude = FMath::Clamp(
		ThirdPersonRecoilLocTarget.Size() / FMath::Max(RecoilHandLocScale.Size(), 1.0f),
		0.0f,
		1.0f
	);

	ThirdPersonRecoilAlpha = RecoilMagnitude;
	ThirdPersonRecoilLoc = ThirdPersonRecoilLocTarget;
	ThirdPersonRecoilRot = ThirdPersonRecoilRotTarget;
	ThirdPersonRecoilSpineLoc = RecoilSpineLocScale * RecoilMagnitude;
	ThirdPersonRecoilSpineRot = ScaleRotator(RecoilSpineRotScale, RecoilMagnitude);
}

void UShooterAnimInstance::UpdateThirdPersonWallOffset(float DeltaSeconds, AWeaponBase* CurrentWeapon, const FWeaponValues* WeaponValues)
{
	float TargetWallAlpha = 0.0f;
	float TargetWallAvoidUpAlpha = 0.0f;
	float TargetWallAvoidDownAlpha = 0.0f;
	float TargetWallMuzzleBlockAlpha = 0.0f;
	float TargetWallVeryCloseAlpha = 0.0f;
	float TargetWallHardStopAlpha = 0.0f;

	if (CachedShooterCharacter && CurrentWeapon && IsFirearmWeapon(CurrentWeaponType) && !bIsDead &&
		!bIsSprinting && !bIsSliding && !bIsReloading)
	{
		// ── 1. 프로브 기준 좌표계 ─────────────────────────────────────────
		// 포즈와 무관한 기준(가슴 높이 + 조준 방향)에서 트레이스한다. 실제
		// Muzzle 소켓에서 쏘면 벽 보정이 측정값에 되먹임되어 경계에서 진동한다
		const FVector TraceForward = CachedShooterCharacter->GetBaseAimRotation().Vector().GetSafeNormal();
		const FVector TraceStart =
			CachedShooterCharacter->GetActorLocation() +
			FVector(0.0f, 0.0f, ThirdPersonWallTraceHeightOffset) +
			TraceForward * ThirdPersonWallTraceForwardOffset;

		const float TraceDistance = FMath::Max(ThirdPersonWallTraceDistance, KINDA_SMALL_NUMBER);
		const float SafeDistance = FMath::Max(ThirdPersonWallSafeDistance, KINDA_SMALL_NUMBER);
		const float TraceRadius = FMath::Max(ThirdPersonWallTraceRadius, KINDA_SMALL_NUMBER);
		const float VerticalProbeOffset = FMath::Max(ThirdPersonWallVerticalProbeOffset, 0.0f);
		const float SideProbeOffset = FMath::Max(ThirdPersonWallSideProbeOffset, TraceRadius * 1.5f);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(TPProceduralWallOffset), false);
		Params.AddIgnoredActor(CachedShooterCharacter);
		Params.AddIgnoredActor(CurrentWeapon);

		struct FThirdPersonWallProbeResult
		{
			float Alpha = 0.0f;
			FHitResult Hit;
			bool bHit = false;
		};

		const auto SweepWallProbe = [&](const FVector& ProbeStart, float ProbeDistance, float ProbeSafeDistance, float ProbeRadius)
		{
			FThirdPersonWallProbeResult Result;
			const FVector ProbeEnd = ProbeStart + TraceForward * ProbeDistance;
			Result.bHit = CachedShooterCharacter->GetWorld()->SweepSingleByChannel(
				Result.Hit,
				ProbeStart,
				ProbeEnd,
				FQuat::Identity,
				ECC_Visibility,
				FCollisionShape::MakeSphere(FMath::Max(ProbeRadius, KINDA_SMALL_NUMBER)),
				Params
			);

			if (Result.bHit)
			{
				Result.Alpha = FMath::Clamp((ProbeDistance - Result.Hit.Distance) / ProbeSafeDistance, 0.0f, 1.0f);
			}

			return Result;
		};

		const FRotationMatrix TraceRotationMatrix(TraceForward.Rotation());
		const FVector TraceRight = TraceRotationMatrix.GetUnitAxis(EAxis::Y);
		const FVector TraceUp = TraceRotationMatrix.GetUnitAxis(EAxis::Z);

		const FThirdPersonWallProbeResult CenterProbe = SweepWallProbe(TraceStart, TraceDistance, SafeDistance, TraceRadius);
		const FThirdPersonWallProbeResult UpperProbe = SweepWallProbe(TraceStart + TraceUp * VerticalProbeOffset, TraceDistance, SafeDistance, TraceRadius);
		const FThirdPersonWallProbeResult LowerProbe = SweepWallProbe(TraceStart - TraceUp * VerticalProbeOffset, TraceDistance, SafeDistance, TraceRadius);
		const FThirdPersonWallProbeResult RightProbe = SweepWallProbe(TraceStart + TraceRight * SideProbeOffset, TraceDistance, SafeDistance, TraceRadius);
		const FThirdPersonWallProbeResult LeftProbe = SweepWallProbe(TraceStart - TraceRight * SideProbeOffset, TraceDistance, SafeDistance, TraceRadius);

		// 조준 방향 프로브는 위/아래를 보면 벽에서 벗어나 버린다 — 몸은 그대로
		// 벽에 붙어 있는데 근접 단계(와 Tight)가 풀려서 숙인 총이 벽을 뚫는다.
		// 수평화한 조준 방향으로도 스윕해서 근접 단계가 몸-벽 거리를 따라가게 한다
		FThirdPersonWallProbeResult FlatProbe;
		FVector FlatForward(TraceForward.X, TraceForward.Y, 0.0f);
		if (FlatForward.Normalize())
		{
			const FVector FlatStart =
				CachedShooterCharacter->GetActorLocation() +
				FVector(0.0f, 0.0f, ThirdPersonWallTraceHeightOffset) +
				FlatForward * ThirdPersonWallTraceForwardOffset;
			FlatProbe.bHit = CachedShooterCharacter->GetWorld()->SweepSingleByChannel(
				FlatProbe.Hit,
				FlatStart,
				FlatStart + FlatForward * TraceDistance,
				FQuat::Identity,
				ECC_Visibility,
				FCollisionShape::MakeSphere(TraceRadius),
				Params
			);
			if (FlatProbe.bHit)
			{
				FlatProbe.Alpha = FMath::Clamp((TraceDistance - FlatProbe.Hit.Distance) / SafeDistance, 0.0f, 1.0f);
			}
		}

		// "벽 바깥쪽" 방향(수평화된 노멀)을 기억해 둔다. 손 당김의 세기 판정
		// (FacingAlpha)에 쓰이며, 조준과 몸 yaw가 벌어져도(제자리 회전,
		// AimOffset 억제) 기준이 흔들리지 않는다
		FVector WallNormalFlat = FVector::ZeroVector;
		if (FlatProbe.bHit)
		{
			WallNormalFlat = FlatProbe.Hit.ImpactNormal;
		}
		else if (CenterProbe.bHit)
		{
			WallNormalFlat = CenterProbe.Hit.ImpactNormal;
		}
		WallNormalFlat.Z = 0.0f;
		if (WallNormalFlat.Normalize())
		{
			ThirdPersonWallPullDirWorld = WallNormalFlat;
		}
		else if (ThirdPersonWallPullDirWorld.IsNearlyZero())
		{
			// 아직 아무 것도 측정 못 한 경우(예: 1인칭 union으로 먼저 단계가
			// 걸린 경우)의 폴백: 조준 정반대 방향
			ThirdPersonWallPullDirWorld = -FlatForward;
		}

		// ── 2. 근접 단계 타깃 (VeryClose / HardStop) ──────────────────────
		TargetWallAlpha = CenterProbe.Alpha;
		// 상단 경계 케이스(1인칭의 WallCeiling에 해당): 위 프로브만 벽을 보면
		// center 기반 근접 단계는 안 걸린 채 아래 회피 포즈가 총을 경계 아래
		// 벽면으로 눌러 넣는다. 경계 알파를 VeryClose/HardStop에 흘려서
		// 손 당김이 같이 걸리게 한다
		const float CeilingEdgeAlpha = FMath::Clamp(
			UpperProbe.Alpha - FMath::Max(CenterProbe.Alpha, LowerProbe.Alpha),
			0.0f,
			1.0f
		);
		const float VeryCloseStartAlpha = FMath::Min(ThirdPersonWallVeryCloseStartAlpha, ThirdPersonWallVeryCloseFullAlpha - KINDA_SMALL_NUMBER);
		TargetWallVeryCloseAlpha = FMath::GetMappedRangeValueClamped(
			FVector2D(VeryCloseStartAlpha, FMath::Max(ThirdPersonWallVeryCloseFullAlpha, VeryCloseStartAlpha + KINDA_SMALL_NUMBER)),
			FVector2D(0.0f, 1.0f),
			FMath::Max3(
				CenterProbe.Alpha,
				FlatProbe.Alpha,
				CeilingEdgeAlpha * FMath::Clamp(ThirdPersonWallCeilingVeryCloseScale, 0.0f, 1.0f)
			)
		);
		// HardStop은 실제 히트 거리로 램프한다. 선형 프로브 알파는 벽이 아직
		// 멀 때 이미 1로 포화되어 정작 필요한 최근접 정보를 잃기 때문
		const float HardStopClearance = FMath::Max(ThirdPersonWallHardStopClearance, 0.0f);
		const float HardStopRange = FMath::Max(ThirdPersonWallHardStopRange, KINDA_SMALL_NUMBER);
		const auto HardStopAlphaFromDistance = [&](float Distance)
		{
			return FMath::Clamp((HardStopClearance + HardStopRange - Distance) / HardStopRange, 0.0f, 1.0f);
		};
		TargetWallHardStopAlpha = CenterProbe.bHit
			? HardStopAlphaFromDistance(CenterProbe.Hit.Distance)
			: 0.0f;
		if (FlatProbe.bHit)
		{
			TargetWallHardStopAlpha = FMath::Max(
				TargetWallHardStopAlpha,
				HardStopAlphaFromDistance(FlatProbe.Hit.Distance)
			);
		}
		if (UpperProbe.bHit && CeilingEdgeAlpha > KINDA_SMALL_NUMBER)
		{
			TargetWallHardStopAlpha = FMath::Max(
				TargetWallHardStopAlpha,
				HardStopAlphaFromDistance(UpperProbe.Hit.Distance) *
					CeilingEdgeAlpha *
					FMath::Clamp(ThirdPersonWallCeilingHardStopScale, 0.0f, 1.0f)
			);
		}
		// ── 3. 회피 포즈 타깃 (경계 검출) ─────────────────────────────────
		// 아래 프로브만 벽을 보면 위로 피하고, 위 프로브만 보면(상단 경계)
		// 아래로 피한다
		TargetWallAvoidUpAlpha = FMath::Clamp(
			LowerProbe.Alpha - FMath::Max(CenterProbe.Alpha, UpperProbe.Alpha),
			0.0f,
			1.0f
		);
		TargetWallAvoidDownAlpha = CeilingEdgeAlpha;

		// 좌/우 한쪽만 벽을 보는 정도 — 세로 모서리(SideEdge) 판정에 사용
		const float RightOnlyAlpha = FMath::Clamp(RightProbe.Alpha - LeftProbe.Alpha, 0.0f, 1.0f);
		const float LeftOnlyAlpha = FMath::Clamp(LeftProbe.Alpha - RightProbe.Alpha, 0.0f, 1.0f);

		// ── 4. 총구/총열 차단 프로브 ──────────────────────────────────────
		// 가파르게 위/아래를 조준하면 총열이 벽에 나란히 눕기 때문에 프로브를 넓힌다
		const float PitchBlockAlpha = FMath::Clamp(FMath::Abs(TraceForward.Z), 0.0f, 1.0f);
		const float PitchRadiusBoost = FMath::Max(ThirdPersonWallPitchBlockTraceRadiusBoost, 0.0f) * PitchBlockAlpha;

		const float MuzzleBlockTraceDistance = FMath::Max(ThirdPersonWallMuzzleBlockTraceDistance, 0.0f);
		if (MuzzleBlockTraceDistance > KINDA_SMALL_NUMBER)
		{
			const FThirdPersonWallProbeResult MuzzleBlockProbe = SweepWallProbe(
				TraceStart,
				MuzzleBlockTraceDistance,
				FMath::Max(ThirdPersonWallMuzzleBlockSafeDistance, KINDA_SMALL_NUMBER),
				FMath::Max(ThirdPersonWallMuzzleBlockTraceRadius + PitchRadiusBoost, KINDA_SMALL_NUMBER)
			);
			TargetWallMuzzleBlockAlpha = MuzzleBlockProbe.Alpha;
			if (MuzzleBlockProbe.bHit)
			{
				TargetWallHardStopAlpha = FMath::Max(
					TargetWallHardStopAlpha,
					HardStopAlphaFromDistance(MuzzleBlockProbe.Hit.Distance)
				);
			}
		}

		// 총열 측면 커버: 총구 기준점 뒤쪽 구간을 스윕해서, 조준 레이는 벽을
		// 비껴가도 벽에 나란히 누운 총열이 감지되게 한다
		const float BarrelBlockLength = FMath::Max(
			ThirdPersonWallBarrelBlockLength + FMath::Max(ThirdPersonWallPitchBlockLengthBoost, 0.0f) * PitchBlockAlpha,
			0.0f
		);
		if (BarrelBlockLength > KINDA_SMALL_NUMBER)
		{
			const FThirdPersonWallProbeResult BarrelBlockProbe = SweepWallProbe(
				TraceStart - TraceForward * BarrelBlockLength,
				BarrelBlockLength,
				FMath::Max(ThirdPersonWallBarrelBlockSafeDistance, KINDA_SMALL_NUMBER),
				FMath::Max(ThirdPersonWallBarrelBlockTraceRadius + PitchRadiusBoost, KINDA_SMALL_NUMBER)
			);
			TargetWallMuzzleBlockAlpha = FMath::Max(TargetWallMuzzleBlockAlpha, BarrelBlockProbe.Alpha);
			if (BarrelBlockProbe.bHit)
			{
				TargetWallHardStopAlpha = FMath::Max(
					TargetWallHardStopAlpha,
					HardStopAlphaFromDistance(FMath::Max(BarrelBlockLength - BarrelBlockProbe.Hit.Distance, 0.0f))
				);
			}
		}

		// ── 5. 1인칭과의 상태 통일 (FP → TP 단방향 union) ─────────────────
		// 1인칭의 raw 타깃과 max해서, 저쪽에서 HardStop/VeryClose가 걸리면
		// WallTight도 같이 걸리게 한다. 반드시 raw 타깃만 가져올 것 — 보간된
		// 알파를 섞으면 서로 값을 붙잡아 영원히 안 풀린다. 역방향(TP→FP)은
		// 하지 않는다: 몸은 벽에 붙었지만 카메라 방향은 뚫려 있는 상황에서
		// 뷰모델이 이유 없이 당겨진다. tilt 계산보다 먼저 실행해야 수입된
		// 근접 단계가 tilt 포즈를 억제해서 WallTight와 중첩되지 않는다
		if (CachedShooterCharacter->IsLocallyControlled())
		{
			if (const USkeletalMeshComponent* FirstPersonMesh = CachedShooterCharacter->GetFirstPersonMesh())
			{
				if (const UShooterFirstPersonAnimInstance* FirstPersonAnimInstance =
						Cast<UShooterFirstPersonAnimInstance>(FirstPersonMesh->GetAnimInstance()))
				{
					TargetWallVeryCloseAlpha = FMath::Max(
						TargetWallVeryCloseAlpha,
						FirstPersonAnimInstance->GetWallVeryCloseOwnTargetAlpha()
					);
					TargetWallHardStopAlpha = FMath::Max(
						TargetWallHardStopAlpha,
						FirstPersonAnimInstance->GetWallHardStopOwnTargetAlpha()
					);
				}
			}
		}

		// ── 6. tilt 포즈 (AvoidUp/Down) ───────────────────────────────────
		// MuzzleBlock은 총을 뒤로 빼는 대신 기울이는 걸 우선한다. 이미 아래를
		// 보고 있으면 아래로, 아니면 위로. HardStop은 WallTight 포즈를 따로
		// 구동하므로 여기의 tilt에는 관여하지 않는다
		const float LookDownAlpha = FMath::Clamp((-TraceForward.Z - 0.12f) / 0.35f, 0.0f, 1.0f);
		// 1인칭의 근접 단계 포즈 억제를 미러링: VeryClose/HardStop이 벽 대응을
		// 담당하면 tilt 포즈는 페이드아웃. 안 그러면 아래 tilt가 총구를 벽
		// 밑동으로 계속 눌러 넣는다
		const float CloseStagePoseSuppress =
			1.0f - FMath::Clamp(FMath::Max(TargetWallVeryCloseAlpha, TargetWallHardStopAlpha), 0.0f, 1.0f);
		// 세로 모서리 케이스: 한쪽 side 프로브만 벽을 보고 center는 비어 있으면
		// 조준이 모서리를 스쳐 지나가는 상황이다. 반경 넓은 muzzle 프로브가
		// 벽면을 스치더라도 tilt 포즈를 크게 올리면 안 된다 (1인칭 프로브는
		// 이 상황에서 아무것도 감지하지 않는다)
		const float SideEdgeAlpha = FMath::Clamp(
			FMath::Max(RightOnlyAlpha, LeftOnlyAlpha) - CenterProbe.Alpha,
			0.0f,
			1.0f
		);
		const float TiltPreferenceAlpha =
			TargetWallMuzzleBlockAlpha *
			FMath::Clamp(ThirdPersonWallMuzzleBlockAvoidUpScale, 0.0f, 1.0f) *
			CloseStagePoseSuppress *
			(1.0f - SideEdgeAlpha);
		TargetWallAvoidUpAlpha = FMath::Max(TargetWallAvoidUpAlpha, TiltPreferenceAlpha * (1.0f - LookDownAlpha));
		TargetWallAvoidDownAlpha = FMath::Max(TargetWallAvoidDownAlpha, TiltPreferenceAlpha * LookDownAlpha);
	}

	// ── 7. 보간 ──────────────────────────────────────────────────────────
	const auto InterpWall = [&](float Current, float Target)
	{
		return SnapWallAlpha(
			InterpAnimAlpha(Current, Target, DeltaSeconds, ThirdPersonWallBlendInSpeed, ThirdPersonWallBlendOutSpeed),
			Target
		);
	};
	ThirdPersonWallOffsetAlpha = InterpWall(ThirdPersonWallOffsetAlpha, TargetWallAlpha);
	ThirdPersonWallAvoidUpPoseAlpha = InterpWall(ThirdPersonWallAvoidUpPoseAlpha, TargetWallAvoidUpAlpha);
	ThirdPersonWallAvoidDownPoseAlpha = InterpWall(ThirdPersonWallAvoidDownPoseAlpha, TargetWallAvoidDownAlpha);
	ThirdPersonWallMuzzleBlockAlpha = InterpWall(ThirdPersonWallMuzzleBlockAlpha, TargetWallMuzzleBlockAlpha);
	ThirdPersonWallVeryCloseAlpha = InterpWall(ThirdPersonWallVeryCloseAlpha, TargetWallVeryCloseAlpha);
	ThirdPersonWallHardStopAlpha = InterpWall(ThirdPersonWallHardStopAlpha, TargetWallHardStopAlpha);

	// ── 8. 손 당김 출력 ──────────────────────────────────────────────────
	// 회피 포즈가 이미 벽을 처리 중이면 기본 당김은 억제한다.
	// 근접 단계는 최후의 뚫림 방지로 항상 살아 있다
	const float PoseSuppress = 1.0f - FMath::Clamp(
		FMath::Max(ThirdPersonWallAvoidUpPoseAlpha, ThirdPersonWallAvoidDownPoseAlpha) * ThirdPersonWallPoseSuppressScale,
		0.0f,
		1.0f
	);
	// 단계별 당김을 1인칭 뷰모델처럼 합산해서, 벽이 가까워질수록 총 당김
	// 예산이 늘어나게 한다 (한 단계 크기로 캡되지 않도록)
	const float BaseWallAlpha =
		FMath::Max(ThirdPersonWallOffsetAlpha, ThirdPersonWallMuzzleBlockAlpha) * PoseSuppress;
	const float WallMultiplier = FMath::Max(WeaponValues ? WeaponValues->ThirdPersonWallOffsetMultiplier : 1.0f, 0.0f);
	const FVector WallWeaponHandLoc = WeaponValues ? WeaponValues->ThirdPersonWallWeaponHandLoc : ThirdPersonWallWeaponHandLoc;
	const FRotator WallWeaponHandRot = WeaponValues ? WeaponValues->ThirdPersonWallWeaponHandRot : ThirdPersonWallWeaponHandRot;
	// 튜닝값 해석: X = 조준 축 뒤 방향 당김(음수), Z = 아래. 당김이 무기 축과
	// 어긋난 옆방향으로 가면 곧바로 "팔 늘어남"으로 보이므로, 수평화한 조준
	// 방향으로 월드에서 만들고 조준이 벽을 정면으로 볼수록 세게(평행/반대면 0,
	// 포즈만 남음) 페이드한 뒤 ModifyBone용 컴포넌트 공간으로 변환한다
	const FVector PullSum =
		(WallWeaponHandLoc * BaseWallAlpha +
		 ThirdPersonWallVeryCloseHandLoc * ThirdPersonWallVeryCloseAlpha +
		 ThirdPersonWallHardStopHandLoc * ThirdPersonWallHardStopAlpha) * WallMultiplier;
	ThirdPersonWallOffsetLoc = FVector::ZeroVector;
	float WallFacingAlpha = 0.0f;
	if (CachedShooterCharacter)
	{
		FVector FlatAim = CachedShooterCharacter->GetBaseAimRotation().Vector();
		FlatAim.Z = 0.0f;
		if (FlatAim.Normalize())
		{
			WallFacingAlpha = ThirdPersonWallPullDirWorld.IsNearlyZero()
				? 1.0f
				: FMath::Clamp(-(FlatAim | ThirdPersonWallPullDirWorld), 0.0f, 1.0f);
			const FVector WorldPull =
				(FlatAim * PullSum.X + FVector(0.0f, 0.0f, PullSum.Z)) * WallFacingAlpha;
			if (const USkeletalMeshComponent* OwnerComp = GetOwningComponent())
			{
				ThirdPersonWallOffsetLoc = OwnerComp->GetComponentTransform().InverseTransformVector(WorldPull);
			}
		}
	}
	ThirdPersonWallOffsetRot = ScaleRotator(
		WallWeaponHandRot,
		FMath::Clamp(BaseWallAlpha + ThirdPersonWallHardStopAlpha, 0.0f, 1.0f) * WallMultiplier * WallFacingAlpha
	);

	// ── 9. Hip/ADS 분할 ─────────────────────────────────────────────────
	// 벽 포즈 애셋은 Hip 기반/ADS 기반 두 벌이 있다. 알파를 ADS 상태로 나눠서
	// 그래프가 둘 사이를 크로스페이드하게 한다 (Hip알파 + ADS알파 = 통합값)
	const float WallPoseADSBlend = FMath::Clamp(ThirdPersonADSAlpha, 0.0f, 1.0f);
	ThirdPersonWallTightAlpha = ThirdPersonWallHardStopAlpha * (1.0f - WallPoseADSBlend);
	ThirdPersonWallTightADSAlpha = ThirdPersonWallHardStopAlpha * WallPoseADSBlend;
	ThirdPersonWallAvoidUpHipPoseAlpha = ThirdPersonWallAvoidUpPoseAlpha * (1.0f - WallPoseADSBlend);
	ThirdPersonWallAvoidUpADSPoseAlpha = ThirdPersonWallAvoidUpPoseAlpha * WallPoseADSBlend;
	ThirdPersonWallAvoidDownHipPoseAlpha = ThirdPersonWallAvoidDownPoseAlpha * (1.0f - WallPoseADSBlend);
	ThirdPersonWallAvoidDownADSPoseAlpha = ThirdPersonWallAvoidDownPoseAlpha * WallPoseADSBlend;
}

void UShooterAnimInstance::UpdateThirdPersonProceduralState(float DeltaSeconds, AWeaponBase* CurrentWeapon)
{
	const bool bHasWeapon = CurrentWeapon != nullptr;
	const bool bCanUseWeaponProcedural = bHasWeapon && IsFirearmWeapon(CurrentWeaponType) && !bIsDead;
	const bool bFireState = CombatState == ECombatState::Fire;
	const bool bAimIntent = bCanUseWeaponProcedural && (bIsAiming || bFireState) && !bIsReloading;
	const bool bADSIntent = bCanUseWeaponProcedural && bIsAiming && !bIsReloading && !bIsSprinting;
	const bool bUpperBodyIntent = bCanUseWeaponProcedural;
	const FWeaponValues* WeaponValues = nullptr;
	if (CurrentWeapon)
	{
		if (const UProceduralAnimValues* ProceduralValues = CurrentWeapon->GetFirstPersonProceduralValues())
		{
			WeaponValues = &ProceduralValues->WeaponValues;
		}
	}

	ThirdPersonAimAlpha = InterpAnimAlpha(
		ThirdPersonAimAlpha,
		bAimIntent ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonAimBlendInSpeed,
		ThirdPersonAimBlendOutSpeed
	);
	// 벽이 가까워지면 ADS를 해제한다. 임계값은 무기 공유 데이터(WeaponValues)에서
	// 읽어 1인칭/3인칭이 같은 조건으로 해제되고, 결과 알파는 1인칭이 재사용하도록
	// publish된다 (GetWallAimBreakAlpha)
	const float WallAimBreakStart = FMath::Clamp(
		WeaponValues ? WeaponValues->WallAimBreakStartAlpha : ThirdPersonWallAimBreakStartAlpha,
		0.0f,
		1.0f
	);
	const float WallAimBreakFull = FMath::Max(
		FMath::Clamp(
			WeaponValues ? WeaponValues->WallAimBreakFullAlpha : ThirdPersonWallAimBreakFullAlpha,
			0.0f,
			1.0f
		),
		WallAimBreakStart + KINDA_SMALL_NUMBER
	);
	ThirdPersonWallAimBreakAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(WallAimBreakStart, WallAimBreakFull),
		FVector2D(0.0f, 1.0f),
		FMath::Max3(ThirdPersonWallVeryCloseAlpha, ThirdPersonWallHardStopAlpha, ThirdPersonWallMuzzleBlockAlpha)
	);
	ThirdPersonADSAlpha = InterpAnimAlpha(
		ThirdPersonADSAlpha,
		bADSIntent ? (1.0f - ThirdPersonWallAimBreakAlpha) : 0.0f,
		DeltaSeconds,
		ThirdPersonAimBlendInSpeed,
		ThirdPersonAimBlendOutSpeed
	);
	const bool bCanUseAimOffset = bCanUseWeaponProcedural && !bIsSprinting;
	ThirdPersonAimOffsetAlpha = InterpAnimAlpha(
		ThirdPersonAimOffsetAlpha,
		bCanUseAimOffset ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonAimBlendInSpeed,
		ThirdPersonAimBlendOutSpeed
	);

	// 벽 포즈와 AimOffset이 팔을 두고 서로 싸우므로, 벽 포즈가 걸려 있는 동안
	// AimOffset을 페이드한다 (이전 프레임의 이미 스무딩된 알파 사용)
	const float WallAimOffsetSuppressAlpha = FMath::Clamp(
		FMath::Max3(ThirdPersonWallAvoidUpPoseAlpha, ThirdPersonWallAvoidDownPoseAlpha, ThirdPersonWallHardStopAlpha) *
			ThirdPersonWallAimOffsetSuppressScale,
		0.0f,
		1.0f
	);
	const float ClampedAimOffsetAlpha =
		FMath::Clamp(ThirdPersonAimOffsetAlpha, 0.0f, 1.0f) * (1.0f - WallAimOffsetSuppressAlpha);
	const float ClampedADSAlpha = FMath::Clamp(ThirdPersonADSAlpha, 0.0f, 1.0f);
	ThirdPersonHipAimOffsetAlpha = ClampedAimOffsetAlpha * (1.0f - ClampedADSAlpha);
	ThirdPersonADSAimOffsetAlpha = ClampedAimOffsetAlpha * ClampedADSAlpha;

	ThirdPersonUpperBodyAlpha = InterpAnimAlpha(
		ThirdPersonUpperBodyAlpha,
		bUpperBodyIntent ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonUpperBodyBlendInSpeed,
		ThirdPersonUpperBodyBlendOutSpeed
	);

	const bool bCanTurnInPlace = !bIsDead && bIsGrounded && !bIsInAir && !bIsSliding && Speed <= ThirdPersonTurnInPlaceMinSpeed;
	const float TurnYaw = UpdateTurnInPlaceYaw(DeltaSeconds, bCanTurnInPlace);
	const float AbsTurnYaw = FMath::Abs(TurnYaw);
	const float TurnTargetAlpha = bCanTurnInPlace && !bTurnInPlaceResetRequested
		? (bIsTurnInPlaceConsuming ? 1.0f : FMath::GetMappedRangeValueClamped(
			FVector2D(ThirdPersonTurnInPlaceStartYaw, ThirdPersonTurnInPlaceFullYaw),
			FVector2D(0.0f, 1.0f),
			AbsTurnYaw))
		: 0.0f;

	if (bTurnInPlaceResetRequested)
	{
		ThirdPersonTurnInPlaceAlpha = 0.0f;
	}
	else
	{
		ThirdPersonTurnInPlaceAlpha = InterpAnimAlpha(
			ThirdPersonTurnInPlaceAlpha,
			TurnTargetAlpha,
			DeltaSeconds,
			ThirdPersonTurnInPlaceBlendInSpeed,
			ThirdPersonTurnInPlaceBlendOutSpeed
		);
	}

	TurnInPlaceYaw = TurnYaw;

	if (bTurnInPlaceResetRequested || !bCanTurnInPlace || (!bIsTurnInPlaceConsuming && TurnTargetAlpha <= KINDA_SMALL_NUMBER))
	{
		TurnInPlaceDirection = 0.0f;
	}
	else
	{
		TurnInPlaceDirection = TurnYaw >= 0.0f ? 1.0f : -1.0f;
	}

	ThirdPersonTurnInPlacePlayRate = FMath::GetMappedRangeValueClamped(
		FVector2D(ThirdPersonTurnInPlaceStartYaw, ThirdPersonTurnInPlaceFullYaw),
		FVector2D(ThirdPersonTurnInPlaceMinPlayRate, ThirdPersonTurnInPlaceMaxPlayRate),
		AbsTurnYaw
	);

	ThirdPersonFireAlpha = InterpAnimAlpha(
		ThirdPersonFireAlpha,
		bCanUseWeaponProcedural && bFireState ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonReloadAlpha = InterpAnimAlpha(
		ThirdPersonReloadAlpha,
		bCanUseWeaponProcedural && bIsReloading ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonSprintPoseAlpha = InterpAnimAlpha(
		ThirdPersonSprintPoseAlpha,
		bCanUseWeaponProcedural && bIsSprinting && !bAimIntent
			? GetThirdPersonSprintPoseTargetAlpha(WeaponValues)
			: 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonSlideAlpha = InterpAnimAlpha(
		ThirdPersonSlideAlpha,
		bCanUseWeaponProcedural && bIsSliding ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);

	UpdateThirdPersonLean(DeltaSeconds);
	UpdateThirdPersonRecoil(DeltaSeconds, WeaponValues);
	UpdateThirdPersonWallOffset(DeltaSeconds, CurrentWeapon, WeaponValues);
}

