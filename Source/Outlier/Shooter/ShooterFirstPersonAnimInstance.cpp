// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"
#include "Shooter/Anim/ProceduralAnimValues.h"

namespace
{
	FVector GetPitchCurveVectorValue(const UCurveVector* Curve, const FVector& FallbackValue, float AimPitch, float MinPitch, float MaxPitch)
	{
		if (!Curve)
		{
			return FallbackValue;
		}

		const float LowPitch = FMath::Min(MinPitch, MaxPitch);
		const float HighPitch = FMath::Max(MinPitch, MaxPitch);
		const float ClampedPitch = FMath::Clamp(AimPitch, LowPitch, HighPitch);
		return Curve->GetVectorValue(ClampedPitch);
	}

	FRotator GetPitchCurveRotatorValue(const UCurveVector* Curve, const FRotator& FallbackValue, float AimPitch, float MinPitch, float MaxPitch)
	{
		if (!Curve)
		{
			return FallbackValue;
		}

		const FVector CurveValue = GetPitchCurveVectorValue(Curve, FVector::ZeroVector, AimPitch, MinPitch, MaxPitch);
		return FRotator(CurveValue.Y, CurveValue.Z, CurveValue.X);
	}

	FRotator BlendRotatorOffset(const FRotator& A, const FRotator& B, float Alpha)
	{
		return FQuat::Slerp(A.Quaternion(), B.Quaternion(), FMath::Clamp(Alpha, 0.0f, 1.0f)).Rotator();
	}

	float GetSmootherStepAlpha(float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return ClampedAlpha * ClampedAlpha * ClampedAlpha * (ClampedAlpha * (ClampedAlpha * 6.0f - 15.0f) + 10.0f);
	}

	float GetEasedAlpha(float Alpha, float Strength)
	{
		const float ClampedStrength = FMath::Clamp(Strength, 0.0f, 3.0f);
		const int32 FullEaseCount = FMath::FloorToInt(ClampedStrength);
		const float PartialEaseAlpha = ClampedStrength - static_cast<float>(FullEaseCount);

		float EasedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		for (int32 EaseIndex = 0; EaseIndex < FullEaseCount; ++EaseIndex)
		{
			EasedAlpha = GetSmootherStepAlpha(EasedAlpha);
		}

		return FMath::Lerp(EasedAlpha, GetSmootherStepAlpha(EasedAlpha), PartialEaseAlpha);
	}
}

void UShooterFirstPersonAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (CachedShooterCharacter)
	{
		CachedShooterCharacter->OnCharacterDeath.RemoveDynamic(this, &UShooterFirstPersonAnimInstance::HandleOwnerDeath);
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	CachedShooterCharacter = Cast<AShooterCharacter>(OwnerPawn);

	if (!CachedShooterCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FPAnim] NativeInitializeAnimation failed OwnerPawn=%s"), *GetNameSafe(OwnerPawn));
		return;
	}

	if (USkeletalMeshComponent* OwningMesh = GetOwningComponent())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s [FPAnim] Init AnimClass=%s Mesh=%s Owner=%s WeaponType=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			*GetClass()->GetName(),
			*GetNameSafe(OwningMesh),
			*GetNameSafe(CachedShooterCharacter),
			static_cast<int32>(CachedShooterCharacter->GetWeaponType()));
	}

	CachedShooterCharacter->OnCharacterDeath.AddUniqueDynamic(this, &UShooterFirstPersonAnimInstance::HandleOwnerDeath);

	bIsSliding = CachedShooterCharacter->IsSliding();
	bIsAiming = CachedShooterCharacter->IsAiming();
	bIsReloading = CachedShooterCharacter->IsReloading();
	bIsDead = CachedShooterCharacter->IsDead();
}

void UShooterFirstPersonAnimInstance::NativeUninitializeAnimation()
{
	if (CachedShooterCharacter)
	{
		CachedShooterCharacter->OnCharacterDeath.RemoveDynamic(this, &UShooterFirstPersonAnimInstance::HandleOwnerDeath);
		CachedShooterCharacter = nullptr;
	}

	Super::NativeUninitializeAnimation();
}

void UShooterFirstPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
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
		CurrentWeapon = nullptr;
		PreviousWeapon = nullptr;
		CurrentWeaponType = EWeaponType::Unarmed;
		bIsCrouching = false;
		bIsSprinting = false;
		bIsSliding = false;
		bIsGrounded = true;
		bIsInAir = false;
		bIsAiming = false;
		bIsReloading = false;
		ViewModelWeaponPoseAlpha = 0.0f;
		ViewModelWeaponEquipDetailAlpha = 0.0f;
		ViewModelSprintExitDetailAlpha = 1.0f;
		SprintExitDetailBlockTimer = 0.0f;
		bWasSprinting = false;
		bHadWeaponPose = false;
		return;
	}

	UCharacterMovementComponent* CharacterMovement = CachedShooterCharacter->GetCharacterMovement();
	if (!CharacterMovement)
	{
		Speed = 0.0f;
		Direction = 0.0f;
		bIsGrounded = true;
		bIsInAir = false;
		return;
	}

	Velocity = CharacterMovement->Velocity;
	GroundSpeed = Velocity.Size2D();
	Speed = GroundSpeed;

	Direction = UKismetAnimationLibrary::CalculateDirection(
		Velocity,
		CachedShooterCharacter->GetActorRotation()
	);

	// 상태 먼저 갱신
	bIsCrouching = CachedShooterCharacter->bIsCrouched;
	const bool bPrevSprinting = bIsSprinting;
	bIsSprinting = CachedShooterCharacter->IsSprinting();
	const bool bSprintJustEnded = bPrevSprinting && !bIsSprinting;

	if (bSprintJustEnded)
	{
		bSkipLeftHandExtraOffsetThisFrame = true;
		bBlockStartStopThisFrame = true;
		StartStopDirection = 0;
		StartStopTime = 0.0f;
	}

	bIsSliding = CachedShooterCharacter->IsSliding();
	bIsAiming = CachedShooterCharacter->IsAiming();
	bIsReloading = CachedShooterCharacter->IsReloading();
	bIsDead = CachedShooterCharacter->IsDead();
	AimPitch = FRotator::NormalizeAxis(CachedShooterCharacter->GetBaseAimRotation().Pitch);

	bIsFalling = CharacterMovement->IsFalling();
	bIsGrounded = CharacterMovement->IsMovingOnGround();
	bIsInAir = bIsFalling;

	AWeaponBase* NewWeapon = CachedShooterCharacter->GetCurrentWeapon();
	const bool bWeaponChanged = NewWeapon != CurrentWeapon;

	if (bWeaponChanged)
	{
		bSkipLeftHandExtraOffsetThisFrame = true;
		ViewModelWeaponPoseAlpha = NewWeapon
			? FMath::Max(ViewModelWeaponPoseAlpha, FMath::Clamp(WeaponPoseEquipInitialAlpha, 0.0f, 1.0f))
			: 0.0f;
		ViewModelWeaponEquipDetailAlpha = 0.0f;
		ViewModelNonSprintProceduralAlpha = 0.0f;
		StartStopDirection = 0;
		StartStopTime = 0.0f;
	}

	CurrentWeapon = NewWeapon;
	PreviousWeapon = CurrentWeapon;
	CurrentWeaponType = CurrentWeapon
		? CurrentWeapon->GetWeaponType()
		: EWeaponType::Unarmed;
	CurrentProceduralValues = CurrentWeapon
		? CurrentWeapon->GetFirstPersonProceduralValues()
		: nullptr;

	// 파생 상태는 그 다음
	bShouldMove = GroundSpeed > 3.0f && !bIsFalling;
	bIsRunOrSprint = bIsSprinting || GroundSpeed > 300.0f;

	const bool bCanUseWeaponPose =
		CurrentWeaponType == EWeaponType::Rifle ||
		CurrentWeaponType == EWeaponType::Pistol ||
		CurrentWeaponType == EWeaponType::Melee;

	const bool bCanUseFirearmProcedural =
		CurrentWeaponType == EWeaponType::Rifle ||
		CurrentWeaponType == EWeaponType::Pistol;

	const bool bStartedWeaponPose = bCanUseWeaponPose && (!bHadWeaponPose || bWeaponChanged);
	const bool bStoppedSprinting = bWasSprinting && !bIsSprinting;

	if (bStartedWeaponPose)
	{
		ViewModelWeaponEquipDetailAlpha = 0.0f;
		PrevAimRot = CachedShooterCharacter->GetBaseAimRotation();
		StartStopTime = 0.0f;
		StartStopDirection = 0;
	}

	if (bStoppedSprinting)
	{
		SprintExitDetailBlockTimer = 0.0f;
		ViewModelSprintExitDetailAlpha = 1.0f;
		PrevAimRot = CachedShooterCharacter->GetBaseAimRotation();
		StartStopTime = 0.0f;
		StartStopDirection = 0;
	}

	ViewModelWeaponPoseAlpha = FMath::FInterpTo(
		ViewModelWeaponPoseAlpha,
		bCanUseWeaponPose ? 1.0f : 0.0f,
		DeltaSeconds,
		bCanUseWeaponPose ? WeaponPoseEquipInterpSpeed : WeaponPoseUnequipInterpSpeed
	);

	const float EquipDetailDelay = FMath::Clamp(WeaponEquipDetailDelayAlpha, 0.0f, 1.0f);
	const float EquipDetailAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(EquipDetailDelay, 1.0f),
		FVector2D(0.0f, 1.0f),
		ViewModelWeaponPoseAlpha
	);
	ViewModelWeaponEquipDetailAlpha = bCanUseWeaponPose ? EquipDetailAlpha : 0.0f;

	const FWeaponValues* WeaponValues = CurrentProceduralValues
		? &CurrentProceduralValues->WeaponValues
		: nullptr;

	const float SprintInSpeed = WeaponValues ? WeaponValues->SprintInterpSpeedIn : 9.0f;
	const float SprintOutSpeed = WeaponValues ? WeaponValues->SprintInterpSpeedOut : 8.0f;
	const float NonSprintInSpeed = WeaponValues ? WeaponValues->NonSprintProceduralBlendInSpeed : 10.0f;
	const float NonSprintOutSpeed = WeaponValues ? WeaponValues->NonSprintProceduralBlendOutSpeed : 12.0f;
	const float ReloadBlendInSpeed = WeaponValues ? WeaponValues->ReloadBlendInSpeed : 18.0f;
	const float ReloadBlendOutSpeed = WeaponValues ? WeaponValues->ReloadBlendOutSpeed : 8.0f;
	const float SlideBlendInSpeed = WeaponValues ? WeaponValues->SlideBlendInSpeed : 18.0f;
	const float SlideBlendOutSpeed = WeaponValues ? WeaponValues->SlideBlendOutSpeed : 8.0f;
	
	ViewModelSprintAlpha = FMath::FInterpTo(
		ViewModelSprintAlpha,
		bIsSprinting ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSprinting ? SprintInSpeed : SprintOutSpeed
	);

	ViewModelReloadPoseAlpha = FMath::FInterpTo(
		ViewModelReloadPoseAlpha,
		(bIsReloading && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsReloading ? ReloadBlendInSpeed : ReloadBlendOutSpeed
	);

	ViewModelSlidePoseAlpha = FMath::FInterpTo(
		ViewModelSlidePoseAlpha,
		(bIsSliding && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSliding ? SlideBlendInSpeed : SlideBlendOutSpeed
	);

	ViewModelEquipPoseAlpha = FMath::Clamp(1.0f - ViewModelWeaponEquipDetailAlpha, 0.0f, 1.0f) * (bCanUseWeaponPose ? 1.0f : 0.0f);

	if (SprintExitDetailBlockTimer > 0.0f)
	{
		SprintExitDetailBlockTimer = FMath::Max(SprintExitDetailBlockTimer - DeltaSeconds, 0.0f);
		ViewModelSprintExitDetailAlpha = 0.0f;
	}
	else
	{
		ViewModelSprintExitDetailAlpha = FMath::FInterpTo(
			ViewModelSprintExitDetailAlpha,
			(!bIsSprinting && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
			DeltaSeconds,
			SprintExitDetailBlendSpeed
		);
	}

	ViewModelNonSprintProceduralAlpha = FMath::FInterpTo(
		ViewModelNonSprintProceduralAlpha,
		(!bIsSprinting && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSprinting ? NonSprintOutSpeed : NonSprintInSpeed
	);

	const bool bNonSprintProceduralReady =
		ViewModelNonSprintProceduralAlpha > 0.95f &&
		ViewModelSprintExitDetailAlpha > 0.95f &&
		ViewModelWeaponEquipDetailAlpha > 0.95f;

	ViewModelAimAlpha = FMath::FInterpTo(
		ViewModelAimAlpha,
		bIsAiming ? 1.0f : 0.0f,
		DeltaSeconds,
		12.0f
	);

	ReloadAimAlpha = FMath::FInterpTo(
		ReloadAimAlpha,
		(bIsAiming && bIsReloading) ? 1.0f : 0.0f,
		DeltaSeconds,
		10.0f
	);

	const bool bWantsWalkAnim =
		bCanUseFirearmProcedural &&
		bShouldMove &&
		!bIsSprinting &&
		!bIsSliding &&
		bNonSprintProceduralReady;

	ViewModelWalkAnimAlpha = FMath::FInterpTo(
		ViewModelWalkAnimAlpha,
		bWantsWalkAnim ? 1.0f : 0.0f,
		DeltaSeconds,
		8.0f
	);

	CrouchAlpha = FMath::FInterpTo(
		CrouchAlpha,
		bIsCrouching ? 1.0f : 0.0f,
		DeltaSeconds,
		16.0f
	);

	const float StrafeSign = FMath::Clamp(Direction / 90.0f, -1.0f, 1.0f);
	const bool bWantsStrafeWalk =
		bCanUseFirearmProcedural &&
		bShouldMove &&
		!bIsSprinting &&
		!bIsSliding &&
		bNonSprintProceduralReady && 
		FMath::Abs(Direction) >= 30.0f &&
		FMath::Abs(Direction) <= 135.0f;
	StrafeWalkAlpha = FMath::FInterpTo(
		StrafeWalkAlpha,
		bWantsStrafeWalk ? 1.0f : 0.0f,
		DeltaSeconds,
		8.0f
	);

	WallOffsetAlpha = FMath::FInterpTo(WallOffsetAlpha, 0.0f, DeltaSeconds, 8.0f);

	ViewModelJumpLandAlpha = FMath::FInterpTo(
		ViewModelJumpLandAlpha,
		bIsFalling ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsFalling ? 8.0f : 12.0f
	);

	if (CurrentProceduralValues)
	{
		ViewModelCurrentStrafeWalkRot = FMath::RInterpTo(
			ViewModelCurrentStrafeWalkRot,
			WeaponValues->StrafeWalkRot * StrafeSign * StrafeWalkAlpha,
			DeltaSeconds,
			8.0f
		);

		StartStopDuration = FMath::Max(WeaponValues->StartStopDuration, KINDA_SMALL_NUMBER);
		const bool bCanPlayStartStop =
			bCanUseFirearmProcedural &&
			!bIsSprinting &&
			bNonSprintProceduralReady &&
			!bBlockStartStopThisFrame;

		if (bCanPlayStartStop)
		{
			if (bShouldMove != bWasShouldMove)
			{
				StartStopTime = 0.0f;
				StartStopDirection = bShouldMove ? 1 : -1;
			}

			if (StartStopDirection != 0)
			{
				StartStopTime += DeltaSeconds;
				const float NormalizedTime = FMath::Clamp(StartStopTime / StartStopDuration, 0.0f, 1.0f);
				const float Pulse = FMath::Sin(NormalizedTime * PI);

				ViewModelStartStopLoc = StartStopDirection > 0
					? WeaponValues->StartMoveLoc * Pulse
					: WeaponValues->StopMoveLoc * Pulse;
				ViewModelStartStopRot = StartStopDirection > 0
					? WeaponValues->StartMoveRot * Pulse
					: WeaponValues->StopMoveRot * Pulse;

				if (NormalizedTime >= 1.0f)
				{
					StartStopDirection = 0;
					ViewModelStartStopLoc = FVector::ZeroVector;
					ViewModelStartStopRot = FRotator::ZeroRotator;
				}
			}
		}
		else
		{
			StartStopTime = 0.0f;
			StartStopDirection = 0;
			ViewModelStartStopLoc = FMath::VInterpTo(
				ViewModelStartStopLoc,
				FVector::ZeroVector,
				DeltaSeconds,
				StartStopDisableBlendSpeed
			);
			ViewModelStartStopRot = FMath::RInterpTo(
				ViewModelStartStopRot,
				FRotator::ZeroRotator,
				DeltaSeconds,
				StartStopDisableBlendSpeed
			);
		}

		bWasShouldMove = bShouldMove;
	}
	else
	{
		ViewModelCurrentStrafeWalkRot = FMath::RInterpTo(ViewModelCurrentStrafeWalkRot, FRotator::ZeroRotator, DeltaSeconds, 8.0f);
		ViewModelStartStopLoc = FVector::ZeroVector;
		ViewModelStartStopRot = FRotator::ZeroRotator;
		StartStopDirection = 0;
		bWasShouldMove = bShouldMove;
	}

	UpdateFirstPersonProceduralValues(DeltaSeconds);

	UpdateViewModelRecoil(DeltaSeconds);
	UpdateFirstPersonProceduralRuntime();

	bWasSprinting = bIsSprinting;
	bHadWeaponPose = bCanUseWeaponPose;
	bBlockStartStopThisFrame = false;
	bSkipLeftHandExtraOffsetThisFrame = false;
}

void UShooterFirstPersonAnimInstance::AddViewModelRecoil(float GameplayRecoilScale)
{
	if (!CurrentProceduralValues)
	{
		return;
	}

	const FRecoilValues& RecoilValues = CurrentProceduralValues->RecoilValues;

	const FVector RandomLoc(
		FMath::RandRange(RecoilValues.RandomLocXMin, RecoilValues.RandomLocXMax),
		FMath::RandRange(RecoilValues.RandomLocYMin, RecoilValues.RandomLocYMax),
		FMath::RandRange(RecoilValues.RandomLocZMin, RecoilValues.RandomLocZMax)
	);

	const FVector RandomRot(
		FMath::RandRange(RecoilValues.RandomRotXMin, RecoilValues.RandomRotXMax),
		FMath::RandRange(RecoilValues.RandomRotYMin, RecoilValues.RandomRotYMax),
		FMath::RandRange(RecoilValues.RandomRotZMin, RecoilValues.RandomRotZMax)
	);

	ViewModelRecoilLoc += (RecoilValues.RecoilAmplitudeLoc+ RandomLoc) * GameplayRecoilScale;
	ViewModelRecoilRot += FRotator::MakeFromEuler(
		(RecoilValues.RecoilAmplitudeRot + RandomRot) * GameplayRecoilScale
	);

	// 연사 시 반동이 무한 누적되어 총/손이 치솟는 것 방지: 누적값에 상한을 둔다(런어웨이 방지용 넉넉한 캡).
	ViewModelRecoilLoc = ViewModelRecoilLoc.GetClampedToMaxSize(4.0f);
	FVector RecoilRotEuler = ViewModelRecoilRot.Euler();
	RecoilRotEuler.X = FMath::Clamp(RecoilRotEuler.X, -10.0f, 10.0f); // Roll
	RecoilRotEuler.Y = FMath::Clamp(RecoilRotEuler.Y, -10.0f, 10.0f); // Pitch
	RecoilRotEuler.Z = FMath::Clamp(RecoilRotEuler.Z, -10.0f, 10.0f); // Yaw
	ViewModelRecoilRot = FRotator::MakeFromEuler(RecoilRotEuler);
	ViewModelFireIKAlpha = 1.0f;
}

void UShooterFirstPersonAnimInstance::UpdateViewModelRecoil(float DeltaSeconds)
{
	const float FireIKAlphaDecaySpeed = CurrentProceduralValues
		? CurrentProceduralValues->WeaponValues.LeftHandFireIKAlphaDecaySpeed
		: 18.0f;
	ViewModelFireIKAlpha = FMath::FInterpTo(
		ViewModelFireIKAlpha,
		0.0f,
		DeltaSeconds,
		FireIKAlphaDecaySpeed
	);

	if (CurrentProceduralValues)
	{
		const FRecoilValues& RecoilValues = CurrentProceduralValues->RecoilValues;

		// 위치 반동: 스프링 보간으로 0 복귀 (Stiffness/Mass/Damping/TargetVelocity 실제 사용)
		ViewModelRecoilLoc = UKismetMathLibrary::VectorSpringInterp(
			ViewModelRecoilLoc, FVector::ZeroVector, RecoilLocSpringState,
			RecoilValues.StiffnessLoc, RecoilValues.CriticalDampingFactorLoc,
			DeltaSeconds, RecoilValues.MassLoc, RecoilValues.TargetVelocityAmountLoc);

		// 회전 반동: Euler(Roll,Pitch,Yaw)로 펼쳐 스프링 보간 후 복원
		FVector RecoilRotEuler = ViewModelRecoilRot.Euler();
		RecoilRotEuler = UKismetMathLibrary::VectorSpringInterp(
			RecoilRotEuler, FVector::ZeroVector, RecoilRotSpringState,
			RecoilValues.StiffnessRot, RecoilValues.CriticalDampingFactorRot,
			DeltaSeconds, RecoilValues.MassRot, RecoilValues.TargetVelocityAmountRot);
		ViewModelRecoilRot = FRotator::MakeFromEuler(RecoilRotEuler);
	}
	else
	{
		ViewModelRecoilLoc = FMath::VInterpTo(ViewModelRecoilLoc, FVector::ZeroVector, DeltaSeconds, 18.0f);
		ViewModelRecoilRot = FMath::RInterpTo(ViewModelRecoilRot, FRotator::ZeroRotator, DeltaSeconds, 18.0f);
	}
}

void UShooterFirstPersonAnimInstance::HandleOwnerDeath()
{
	bIsDead = true;
}

void UShooterFirstPersonAnimInstance::UpdateFirstPersonProceduralValues(float DeltaSeconds)
{
	if (bDebugLeftHandIK)
	{
		LeftHandIKDebugLogTime = FMath::Max(0.0f, LeftHandIKDebugLogTime - DeltaSeconds);
	}

	if (!CurrentProceduralValues)
	{
		ViewModelProceduralRuntime = FFirstPersonProceduralAnimRuntime();
		ViewModelHipPoseLoc = FVector::ZeroVector;
		ViewModelHipPoseRot = FRotator::ZeroRotator;
		ViewModelAimPoseLoc = FVector::ZeroVector;
		ViewModelAimPoseRot = FRotator::ZeroRotator;
		ViewModelSprintPoseLoc = FVector::ZeroVector;
		ViewModelSprintPoseRot = FRotator::ZeroRotator;
		ViewModelLeftHandIKLoc = FVector::ZeroVector;
		ViewModelLeftHandIKRot = FRotator::ZeroRotator;
		ViewModelLeftHandIKAlpha = 0.0f;
		ViewModelWeaponPoseAlpha = 0.0f;
		ViewModelWeaponEquipDetailAlpha = 0.0f;
		ViewModelNonSprintProceduralAlpha = 0.0f;
		ViewModelReloadPoseAlpha = 0.0f;
		ViewModelFireIKAlpha = 0.0f;
		ViewModelEquipPoseAlpha = 0.0f;
		ViewModelSlidePoseAlpha = 0.0f;
		ViewModelSprintExitDetailAlpha = 1.0f;
		SprintExitDetailBlockTimer = 0.0f;
		ViewModelLeftHandJointTargetLoc = FVector::ZeroVector;
		ViewModelPitchIKOffsetLoc = FVector::ZeroVector;
		ViewModelPitchIKOffsetRot = FRotator::ZeroRotator;
		ViewModelLeftUpperArmPitchLoc = FVector::ZeroVector;
		ViewModelLeftUpperArmPitchRot = FRotator::ZeroRotator;
		ViewModelForwardWalkLoc = FVector::ZeroVector;
		ViewModelForwardWalkRot = FRotator::ZeroRotator;
		ViewModelForwardWalkAnimLoc = FVector::ZeroVector;
		ViewModelForwardWalkAnimRot = FRotator::ZeroRotator;
		ViewModelSwayLoc = FVector::ZeroVector;
		ViewModelSwayRot = FRotator::ZeroRotator;
		ViewModelJumpLandLoc = FVector::ZeroVector;
		ViewModelJumpLandRot = FRotator::ZeroRotator;
		ViewModelWallOffsetLoc = FVector::ZeroVector;
		ViewModelWallOffsetRot = FRotator::ZeroRotator;
		FingerMovementAlpha = 0.0f;
		bWasSprinting = false;
		bHadWeaponPose = false;
		return;
	}

	const FWeaponValues& WeaponValues = CurrentProceduralValues->WeaponValues;

	ViewModelHipPoseLoc = WeaponValues.HipPoseLoc;
	ViewModelHipPoseRot = WeaponValues.HipPoseRot;
	ViewModelAimPoseLoc = WeaponValues.AimPoseLoc;
	ViewModelAimPoseRot = WeaponValues.AimPoseRot;

	ViewModelSprintPoseLoc = WeaponValues.SprintPoseLoc;
	ViewModelSprintPoseRot = WeaponValues.SprintPoseRot;

	const float PitchSprintAlpha = GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength);
	const float ProceduralAimPitch = bIsSprinting
		? 0.0f
		: FMath::Lerp(AimPitch, 0.0f, PitchSprintAlpha);
	const FVector StandPitchIKOffsetLoc = GetPitchCurveVectorValue(
		WeaponValues.PitchIKOffsetLocCurve,
		FVector::ZeroVector,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator StandPitchIKOffsetRot = GetPitchCurveRotatorValue(
		WeaponValues.PitchIKOffsetRotCurve,
		FRotator::ZeroRotator,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FVector StandPitchLeftHandJointTargetLoc = GetPitchCurveVectorValue(
		WeaponValues.PitchLeftHandJointTargetLocCurve,
		FVector::ZeroVector,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	
	const FVector StandPitchLeftUpperArmLoc = GetPitchCurveVectorValue(
		WeaponValues.PitchLeftUpperArmLocCurve,
		FVector::ZeroVector,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator StandPitchLeftUpperArmRot = GetPitchCurveRotatorValue(
		WeaponValues.PitchLeftUpperArmRotCurve,
		FRotator::ZeroRotator,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	
	const FVector CrouchPitchIKOffsetLoc = GetPitchCurveVectorValue(
		WeaponValues.CrouchPitchIKOffsetLocCurve,
		StandPitchIKOffsetLoc,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator CrouchPitchIKOffsetRot = GetPitchCurveRotatorValue(
		WeaponValues.CrouchPitchIKOffsetRotCurve,
		StandPitchIKOffsetRot,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FVector CrouchPitchLeftHandJointTargetLoc = GetPitchCurveVectorValue(
		WeaponValues.CrouchPitchLeftHandJointTargetLocCurve,
		StandPitchLeftHandJointTargetLoc,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	
	const FVector CrouchPitchLeftUpperArmLoc = GetPitchCurveVectorValue(
		WeaponValues.CrouchPitchLeftUpperArmLocCurve,
		StandPitchLeftUpperArmLoc,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator CrouchPitchLeftUpperArmRot = GetPitchCurveRotatorValue(
		WeaponValues.CrouchPitchLeftUpperArmRotCurve,
		StandPitchLeftUpperArmRot,
		ProceduralAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	
	const float PitchCrouchAlpha = FMath::Clamp(CrouchAlpha, 0.0f, 1.0f);
	ViewModelPitchIKOffsetLoc = FMath::Lerp(StandPitchIKOffsetLoc, CrouchPitchIKOffsetLoc, PitchCrouchAlpha);
	ViewModelPitchIKOffsetRot = BlendRotatorOffset(StandPitchIKOffsetRot, CrouchPitchIKOffsetRot, PitchCrouchAlpha);
	ViewModelLeftHandJointTargetLoc = FMath::Lerp(StandPitchLeftHandJointTargetLoc, CrouchPitchLeftHandJointTargetLoc, PitchCrouchAlpha);
	ViewModelLeftUpperArmPitchLoc = FMath::Lerp(StandPitchLeftUpperArmLoc, CrouchPitchLeftUpperArmLoc, PitchCrouchAlpha);
	ViewModelLeftUpperArmPitchRot = BlendRotatorOffset(StandPitchLeftUpperArmRot, CrouchPitchLeftUpperArmRot, PitchCrouchAlpha);
	
	ViewModelForwardWalkLoc = WeaponValues.WalkTiltLoc;
	ViewModelForwardWalkRot = WeaponValues.WalkTiltRot;

	WalkCycleTime += DeltaSeconds * GroundSpeed * 0.01f;

	const float WalkPhase = WalkCycleTime * 2.0f;
	const float X = FMath::Sin(WalkPhase);
	const float Z = FMath::Abs(FMath::Cos(WalkPhase));

	// 회전은 위치보다 살짝 늦게 따라오게 해서 기계적인 느낌을 줄인다
	const float WalkRotPhase = WalkPhase - WeaponValues.WalkAnimRotPhaseOffset;
	const float Xr = FMath::Sin(WalkRotPhase);
	const float Zr = FMath::Abs(FMath::Cos(WalkRotPhase));

	ViewModelForwardWalkAnimLoc = FVector(
		WeaponValues.WalkAnimLocAmplitude.X * X,
		WeaponValues.WalkAnimLocAmplitude.Y * X,
		WeaponValues.WalkAnimLocAmplitude.Z * Z
	) * ViewModelWalkAnimAlpha;

	ViewModelForwardWalkAnimRot = FRotator(
		WeaponValues.WalkAnimRotAmplitude.Pitch * Zr,
		WeaponValues.WalkAnimRotAmplitude.Yaw * Xr,
		WeaponValues.WalkAnimRotAmplitude.Roll * Xr
	) * ViewModelWalkAnimAlpha;

	const FRotator CurrentAimRot = CachedShooterCharacter->GetBaseAimRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(PrevAimRot.Yaw, CurrentAimRot.Yaw);
	const float DeltaPitch = FMath::FindDeltaAngleDegrees(PrevAimRot.Pitch, CurrentAimRot.Pitch);

	PrevAimRot = CurrentAimRot;
	ViewModelJumpLandLoc = WeaponValues.JumpLandLoc;
	ViewModelJumpLandRot = WeaponValues.JumpLandRot;
	ViewModelWallOffsetLoc = WeaponValues.WallOffsetLoc * WallOffsetAlpha;
	ViewModelWallOffsetRot = WeaponValues.WallOffsetRot * WallOffsetAlpha;
	FingerMovementAlpha = WeaponValues.FingerMovementAlpha;

	const bool bUseFirearmProcedural =
		CurrentWeaponType == EWeaponType::Rifle ||
		CurrentWeaponType == EWeaponType::Pistol;
	const float FirearmIKAlpha = bUseFirearmProcedural ? 1.0f : 0.0f;
	const float LeftHandWeaponPoseAlpha = FMath::Clamp(ViewModelWeaponPoseAlpha, 0.0f, 1.0f);
	const float LeftHandWeaponDetailStart = FMath::Clamp(WeaponDetailAlphaStart, 0.0f, 1.0f);
	const float LeftHandWeaponDetailEnd = FMath::Max(FMath::Clamp(WeaponDetailAlphaEnd, 0.0f, 1.0f), LeftHandWeaponDetailStart + KINDA_SMALL_NUMBER);
	const float LeftHandRawWeaponDetailAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(LeftHandWeaponDetailStart, LeftHandWeaponDetailEnd),
		FVector2D(0.0f, 1.0f),
		LeftHandWeaponPoseAlpha
	);
	const float LeftHandWeaponDetailAlpha = GetEasedAlpha(LeftHandRawWeaponDetailAlpha, WeaponValues.WeaponDetailAlphaEaseStrength);
	const float LeftHandFirearmAlpha = bUseFirearmProcedural ? LeftHandWeaponDetailAlpha : 0.0f;
	const float LeftHandEquipDetailAlpha = FMath::Clamp(ViewModelWeaponEquipDetailAlpha, 0.0f, 1.0f);
	const float LeftHandSprintExitDetailAlpha = FMath::Clamp(ViewModelSprintExitDetailAlpha, 0.0f, 1.0f);
	const float LeftHandDetailRecoveryAlpha = LeftHandEquipDetailAlpha * LeftHandSprintExitDetailAlpha;
	const float LeftHandNonSprintProceduralAlpha = FMath::Clamp(ViewModelNonSprintProceduralAlpha, 0.0f, 1.0f) * LeftHandDetailRecoveryAlpha;
	const float LeftHandReloadPoseAlpha = FMath::Clamp(ViewModelReloadPoseAlpha, 0.0f, 1.0f);
	const float LeftHandEquipPoseAlpha = FMath::Clamp(ViewModelEquipPoseAlpha, 0.0f, 1.0f);
	const float LeftHandSlidePoseAlpha = FMath::Clamp(ViewModelSlidePoseAlpha, 0.0f, 1.0f);
	const float LeftHandFirePoseAlpha = FMath::Clamp(ViewModelFireIKAlpha, 0.0f, 1.0f);

	const float LeftHandActionIKAlphaScale =
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandReloadIKAlphaScale, 0.0f, 1.0f), LeftHandReloadPoseAlpha) *
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandEquipIKAlphaScale, 0.0f, 1.0f), LeftHandEquipPoseAlpha) *
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandSlideIKAlphaScale, 0.0f, 1.0f), LeftHandSlidePoseAlpha) *
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandFireIKAlphaScale, 0.0f, 1.0f), LeftHandFirePoseAlpha);
	const float LeftHandFirearmMinorProceduralAlpha =
		LeftHandFirearmAlpha *
		LeftHandNonSprintProceduralAlpha;
	const float LeftHandRuntimeSprintAlpha =
		GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength) *
		LeftHandFirearmAlpha;
	const float LeftHandIKAimPitch = FMath::Lerp(
		AimPitch,
		0.0f,
		FMath::Clamp(LeftHandRuntimeSprintAlpha, 0.0f, 1.0f)
	);
	const FVector LeftHandStandPitchIKOffsetLoc = GetPitchCurveVectorValue(
		WeaponValues.PitchIKOffsetLocCurve,
		FVector::ZeroVector,
		LeftHandIKAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator LeftHandStandPitchIKOffsetRot = GetPitchCurveRotatorValue(
		WeaponValues.PitchIKOffsetRotCurve,
		FRotator::ZeroRotator,
		LeftHandIKAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FVector LeftHandCrouchPitchIKOffsetLoc = GetPitchCurveVectorValue(
		WeaponValues.CrouchPitchIKOffsetLocCurve,
		LeftHandStandPitchIKOffsetLoc,
		LeftHandIKAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FRotator LeftHandCrouchPitchIKOffsetRot = GetPitchCurveRotatorValue(
		WeaponValues.CrouchPitchIKOffsetRotCurve,
		LeftHandStandPitchIKOffsetRot,
		LeftHandIKAimPitch,
		WeaponValues.PitchOffsetMin,
		WeaponValues.PitchOffsetMax
	);
	const FVector LeftHandPitchIKOffsetLoc = FMath::Lerp(
		LeftHandStandPitchIKOffsetLoc,
		LeftHandCrouchPitchIKOffsetLoc,
		PitchCrouchAlpha
	);
	const FRotator LeftHandPitchIKOffsetRot = BlendRotatorOffset(
		LeftHandStandPitchIKOffsetRot,
		LeftHandCrouchPitchIKOffsetRot,
		PitchCrouchAlpha
	);
	FVector TargetLeftHandIKLoc = FVector::ZeroVector;
	FRotator TargetLeftHandIKRot = FRotator::ZeroRotator;
	float TargetLeftHandIKAlpha = 0.0f;
	FVector TargetLeftHandGripOffsetLoc = FVector::ZeroVector;
	FRotator TargetLeftHandGripOffsetRot = FRotator::ZeroRotator;
	bool bLeftHandIKSocketValid = false;
	FName DebugLeftHandSocketName = NAME_None;
	USkeletalMeshComponent* DebugWeaponMesh = nullptr;
	USkeletalMeshComponent* DebugArmsMesh = nullptr;
	FVector DebugLeftHandSprintIKLocOffset = FVector::ZeroVector;
	FRotator DebugLeftHandSprintIKRotOffset = FRotator::ZeroRotator;

	if (FirearmIKAlpha > 0.0f && CurrentWeapon)
	{
		USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetFirstPersonWeaponMesh();
		USkeletalMeshComponent* ArmsMesh = GetOwningComponent();
		const FName BaseLeftHandSocketName = CurrentWeapon->GetLeftHandIKSocketName();
		const FName LeftHandSprintSocketName = CurrentWeapon->GetLeftHandSprintIKSocketName();
		const bool bHasBaseLeftHandSocket = WeaponMesh && WeaponMesh->DoesSocketExist(BaseLeftHandSocketName);
		const bool bHasSprintLeftHandSocket =
			WeaponMesh &&
			!LeftHandSprintSocketName.IsNone() &&
			WeaponMesh->DoesSocketExist(LeftHandSprintSocketName);
		const FName DebugSelectedLeftHandSocketName =
			(bHasSprintLeftHandSocket && LeftHandRuntimeSprintAlpha > 0.5f)
				? LeftHandSprintSocketName
				: BaseLeftHandSocketName;
		DebugWeaponMesh = WeaponMesh;
		DebugArmsMesh = ArmsMesh;
		DebugLeftHandSocketName = DebugSelectedLeftHandSocketName;

		if (WeaponMesh && ArmsMesh && (bHasBaseLeftHandSocket || bHasSprintLeftHandSocket))
		{
			bLeftHandIKSocketValid = true;
			const FName FallbackSocketName = bHasBaseLeftHandSocket ? BaseLeftHandSocketName : LeftHandSprintSocketName;
			const FTransform BaseSocketWorldTransform = WeaponMesh->GetSocketTransform(FallbackSocketName, RTS_World);
			FTransform SocketWorldTransform = BaseSocketWorldTransform;
			if (bHasBaseLeftHandSocket && bHasSprintLeftHandSocket)
			{
				const FTransform SprintSocketWorldTransform = WeaponMesh->GetSocketTransform(LeftHandSprintSocketName, RTS_World);
				const float SprintSocketBlendAlpha = FMath::Clamp(LeftHandRuntimeSprintAlpha, 0.0f, 1.0f);
				SocketWorldTransform = FTransform(
					FQuat::Slerp(
						BaseSocketWorldTransform.GetRotation(),
						SprintSocketWorldTransform.GetRotation(),
						SprintSocketBlendAlpha
					).GetNormalized(),
					FMath::Lerp(
						BaseSocketWorldTransform.GetLocation(),
						SprintSocketWorldTransform.GetLocation(),
						SprintSocketBlendAlpha
					),
					FVector::OneVector
				);
			}
			const FTransform ArmsWorldTransform = ArmsMesh->GetComponentTransform();
			const FVector SocketLocalLoc = ArmsWorldTransform.InverseTransformPosition(SocketWorldTransform.GetLocation());
			const FRotator SocketLocalRot = ArmsWorldTransform.InverseTransformRotation(SocketWorldTransform.GetRotation()).Rotator();
			// 무기는 그래프에서 ik_hand_gun 본 기준으로 회전하므로(VB hand_gun -> ik_hand_gun CopyBone),
			// 소켓 재구성의 회전 피벗도 무기 메시 원점이 아니라 그 본 위치를 써야 손이 안 떨어진다.
			const int32 WeaponPivotBoneIndex = ArmsMesh->GetBoneIndex(TEXT("ik_hand_gun"));
			const FVector WeaponPivotLocalLoc = (WeaponPivotBoneIndex != INDEX_NONE)
				? ArmsMesh->GetBoneTransform(WeaponPivotBoneIndex, FTransform::Identity).GetLocation()
				: ArmsWorldTransform.InverseTransformPosition(WeaponMesh->GetComponentLocation());

			// Route 1: 왼손 LeftHandIK 소켓을 ik_hand_gun(총손) 로컬로 변환한 정적 그립 오프셋.
			// 소켓과 총손을 같은(지난) 포즈에서 읽으므로 상대값은 정확하다(강체). 그래프에서 실제 ik_hand_gun에 얹는다.
			if (WeaponPivotBoneIndex != INDEX_NONE)
			{
				const FTransform GunHandCompTransform = ArmsMesh->GetBoneTransform(WeaponPivotBoneIndex, FTransform::Identity);

				// 옛 재구성이 손 타깃에 더하던 튜닝 오프셋(pitch + sprint)을 raw 값으로 소켓에 합쳐 그립 오프셋에 포함.
				// raw를 쓰는 이유: 절차적 회전은 그래프(ik_hand_gun)가 적용하므로 여기서 또 회전시키면 이중 적용된다.
				// sprint가 아닐 땐 LeftHandRuntimeSprintAlpha=0이라 sprint 항은 0 → 기존 동작 그대로.
				const FVector SprintPitchSocketLocRaw = GetPitchCurveVectorValue(
					WeaponValues.LeftHandSprintPitchSocketLocCurve, FVector::ZeroVector, AimPitch,
					WeaponValues.PitchOffsetMin, WeaponValues.PitchOffsetMax) * LeftHandRuntimeSprintAlpha;
				const FRotator SprintPitchSocketRotRaw = GetPitchCurveRotatorValue(
					WeaponValues.LeftHandSprintPitchSocketRotCurve, FRotator::ZeroRotator, AimPitch,
					WeaponValues.PitchOffsetMin, WeaponValues.PitchOffsetMax) * LeftHandRuntimeSprintAlpha;
				const FVector SprintIKLocRaw = WeaponValues.LeftHandSprintIKLocOffset * LeftHandRuntimeSprintAlpha;
				const FRotator SprintIKRotRaw = WeaponValues.LeftHandSprintIKRotOffset * LeftHandRuntimeSprintAlpha;

				const FVector SocketLocWithOffsets =
					SocketLocalLoc + LeftHandPitchIKOffsetLoc + SprintPitchSocketLocRaw + SprintIKLocRaw;
				const FQuat SocketRotWithOffsets = (
					SocketLocalRot.Quaternion() *
					LeftHandPitchIKOffsetRot.Quaternion() *
					SprintPitchSocketRotRaw.Quaternion() *
					SprintIKRotRaw.Quaternion()
				).GetNormalized();
				const FTransform SocketCompTransform(SocketRotWithOffsets, SocketLocWithOffsets);
				const FTransform GripOffset = SocketCompTransform.GetRelativeTransform(GunHandCompTransform);
				TargetLeftHandGripOffsetLoc = GripOffset.GetLocation();
				TargetLeftHandGripOffsetRot = GripOffset.Rotator();
			}

			const bool bSkipLeftHandExtraOffset = bSkipLeftHandExtraOffsetThisFrame;
			const auto GetLeftHandFollowLocScale = [](const FVector& Scale)
			{
				return Scale;
			};
			const auto GetLeftHandFollowRotScale = [](const float Scale)
			{
				return Scale;
			};
			const FVector LeftHandRecoilLocScale = GetLeftHandFollowLocScale(WeaponValues.LeftHandRecoilLocScale);
			const float LeftHandRecoilRotScale = GetLeftHandFollowRotScale(WeaponValues.LeftHandRecoilRotScale);

			const FVector LeftHandSprintLocScale = GetLeftHandFollowLocScale(WeaponValues.LeftHandSprintLocScale);
			const float LeftHandSprintRotScale = GetLeftHandFollowRotScale(WeaponValues.LeftHandSprintRotScale);
			const FVector LeftHandWalkLocScale = GetLeftHandFollowLocScale(WeaponValues.LeftHandWalkLocScale);
			const float LeftHandWalkRotScale = GetLeftHandFollowRotScale(WeaponValues.LeftHandWalkRotScale);
			const FVector LeftHandJumpLocScale = GetLeftHandFollowLocScale(WeaponValues.LeftHandJumpLocScale);
			const float LeftHandJumpRotScale = GetLeftHandFollowRotScale(WeaponValues.LeftHandJumpRotScale);

			const FVector WeaponRecoilLoc = bSkipLeftHandExtraOffset
				? FVector::ZeroVector
				: (ViewModelRecoilLoc * LeftHandFirearmMinorProceduralAlpha) * LeftHandRecoilLocScale;
			const FRotator WeaponRecoilRot = bSkipLeftHandExtraOffset
				? FRotator::ZeroRotator
				: (ViewModelRecoilRot * LeftHandFirearmMinorProceduralAlpha) * LeftHandRecoilRotScale;

			const FVector WeaponSprintLoc = (ViewModelSprintPoseLoc * LeftHandRuntimeSprintAlpha) * LeftHandSprintLocScale;
			const FRotator WeaponSprintRot = (ViewModelSprintPoseRot * LeftHandRuntimeSprintAlpha) * LeftHandSprintRotScale;
			const FVector LeftHandWalkLocOffset =
				(((ViewModelForwardWalkLoc * ViewModelWalkAnimAlpha) + ViewModelForwardWalkAnimLoc) * LeftHandFirearmMinorProceduralAlpha) *
				LeftHandWalkLocScale;
			const FRotator LeftHandWalkRotOffset =
				(((ViewModelForwardWalkRot * ViewModelWalkAnimAlpha) + ViewModelForwardWalkAnimRot + ViewModelCurrentStrafeWalkRot) * LeftHandFirearmMinorProceduralAlpha) *
				LeftHandWalkRotScale;
			const FVector WeaponJumpLoc = (ViewModelJumpLandLoc * ViewModelJumpLandAlpha * LeftHandFirearmMinorProceduralAlpha) * LeftHandJumpLocScale;
			const FRotator WeaponJumpRot = (ViewModelJumpLandRot * ViewModelJumpLandAlpha * LeftHandFirearmMinorProceduralAlpha) * LeftHandJumpRotScale;
			const FVector WeaponStartStopLoc = ViewModelStartStopLoc * LeftHandFirearmMinorProceduralAlpha;
			const FRotator WeaponStartStopRot = ViewModelStartStopRot * LeftHandFirearmMinorProceduralAlpha;
			const FRotator WeaponProceduralRot = (
				WeaponSprintRot.Quaternion() *
				WeaponRecoilRot.Quaternion() *
				LeftHandWalkRotOffset.Quaternion() *
				WeaponStartStopRot.Quaternion() *
				WeaponJumpRot.Quaternion()
			).Rotator();
			const FVector WeaponProceduralLoc =
				WeaponSprintLoc +
				LeftHandWalkLocOffset +
				WeaponJumpLoc +
				WeaponRecoilLoc +
				WeaponStartStopLoc;
			const FRotator FinalWeaponSocketRot = (
				WeaponProceduralRot.Quaternion() *
				SocketLocalRot.Quaternion()
			).Rotator();
			const FVector FinalWeaponSocketLoc =
				WeaponPivotLocalLoc +
				WeaponProceduralLoc +
				FRotationMatrix(WeaponProceduralRot).TransformVector(SocketLocalLoc - WeaponPivotLocalLoc);
			const FVector SprintPitchSocketLocOffset = FRotationMatrix(WeaponProceduralRot).TransformVector(
				GetPitchCurveVectorValue(
					WeaponValues.LeftHandSprintPitchSocketLocCurve,
					FVector::ZeroVector,
					AimPitch,
					WeaponValues.PitchOffsetMin,
					WeaponValues.PitchOffsetMax
				) * LeftHandRuntimeSprintAlpha
			);
			const FRotator SprintPitchSocketRotOffset = GetPitchCurveRotatorValue(
				WeaponValues.LeftHandSprintPitchSocketRotCurve,
				FRotator::ZeroRotator,
				AimPitch,
				WeaponValues.PitchOffsetMin,
				WeaponValues.PitchOffsetMax
			) * LeftHandRuntimeSprintAlpha;
			const FRotator SprintPitchWeaponSocketRot = (
				FinalWeaponSocketRot.Quaternion() *
				SprintPitchSocketRotOffset.Quaternion()
			).Rotator();
			const FVector SprintPitchWeaponSocketLoc =
				FinalWeaponSocketLoc +
				SprintPitchSocketLocOffset;
			const FVector SprintIKLocOffset = FRotationMatrix(SprintPitchWeaponSocketRot).TransformVector(
				WeaponValues.LeftHandSprintIKLocOffset * LeftHandRuntimeSprintAlpha
			);
			const FRotator SprintIKRotOffset = WeaponValues.LeftHandSprintIKRotOffset * LeftHandRuntimeSprintAlpha;
			DebugLeftHandSprintIKLocOffset = SprintIKLocOffset;
			DebugLeftHandSprintIKRotOffset = SprintIKRotOffset;
			
			TargetLeftHandIKLoc =
				SprintPitchWeaponSocketLoc +
				LeftHandPitchIKOffsetLoc +
				SprintIKLocOffset;
			TargetLeftHandIKRot = (
				SprintPitchWeaponSocketRot.Quaternion() *
				LeftHandPitchIKOffsetRot.Quaternion() *
				SprintIKRotOffset.Quaternion()
			).Rotator();

			TargetLeftHandIKAlpha = LeftHandFirearmAlpha * LeftHandActionIKAlphaScale;
		}
	}

	DebugLastLeftHandIKTargetLoc = TargetLeftHandIKLoc;
	DebugLastLeftHandIKTargetRot = TargetLeftHandIKRot;
	DebugLastLeftHandIKTargetAlpha = TargetLeftHandIKAlpha;
	bDebugLastLeftHandIKSocketValid = bLeftHandIKSocketValid;

	if (bDebugLeftHandIK && LeftHandIKDebugLogTime <= 0.0f)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s [FPAnim][LeftHandIK:Target] Weapon=%s Type=%d FirearmIK=%.2f Socket=%s SocketValid=%d WeaponMesh=%s ArmsMesh=%s TargetAlpha=%.2f TargetLoc=%s TargetRot=%s CurrentLoc=%s CurrentRot=%s CurrentAlpha=%.2f WeaponPoseAlpha=%.2f HipLoc=%s HipRot=%s PitchIKLoc=%s SprintIKLoc=%s SprintIKRot=%s RecoilLoc=%s SwayLoc=%s"),
			CachedShooterCharacter ? OutlierNet::GetNetPrefix(CachedShooterCharacter) : TEXT("[NoOwner]"),
			*GetNameSafe(CurrentWeapon),
			static_cast<int32>(CurrentWeaponType),
			FirearmIKAlpha,
			*DebugLeftHandSocketName.ToString(),
			bLeftHandIKSocketValid ? 1 : 0,
			*GetNameSafe(DebugWeaponMesh),
			*GetNameSafe(DebugArmsMesh),
			TargetLeftHandIKAlpha,
			*TargetLeftHandIKLoc.ToCompactString(),
			*TargetLeftHandIKRot.ToCompactString(),
			*ViewModelLeftHandIKLoc.ToCompactString(),
			*ViewModelLeftHandIKRot.ToCompactString(),
			ViewModelLeftHandIKAlpha,
			ViewModelWeaponPoseAlpha,
			*ViewModelHipPoseLoc.ToCompactString(),
			*ViewModelHipPoseRot.ToCompactString(),
			*ViewModelPitchIKOffsetLoc.ToCompactString(),
			*DebugLeftHandSprintIKLocOffset.ToCompactString(),
			*DebugLeftHandSprintIKRotOffset.ToCompactString(),
			*ViewModelRecoilLoc.ToCompactString(),
			*ViewModelSwayLoc.ToCompactString());
	}

	ViewModelLeftHandIKLoc = TargetLeftHandIKLoc;
	ViewModelLeftHandIKRot = TargetLeftHandIKRot;
	ViewModelLeftHandIKAlpha = TargetLeftHandIKAlpha;
	ViewModelLeftHandGripOffsetLoc = TargetLeftHandGripOffsetLoc;
	ViewModelLeftHandGripOffsetRot = TargetLeftHandGripOffsetRot;

	ViewModelMovementLoc = ViewModelSprintPoseLoc;
	ViewModelMovementRot = ViewModelSprintPoseRot;
}

void UShooterFirstPersonAnimInstance::UpdateFirstPersonProceduralRuntime()
{
	if (!CurrentProceduralValues)
	{
		ViewModelProceduralRuntime = FFirstPersonProceduralAnimRuntime();
		return;
	}

	ViewModelProceduralRuntime.HipPoseLoc = ViewModelHipPoseLoc;
	ViewModelProceduralRuntime.HipPoseRot = ViewModelHipPoseRot;
	ViewModelProceduralRuntime.AimPoseLoc = ViewModelAimPoseLoc;
	ViewModelProceduralRuntime.AimPoseRot = ViewModelAimPoseRot;
	ViewModelProceduralRuntime.AimAlpha = ViewModelAimAlpha;
	ViewModelProceduralRuntime.ReloadAimAlpha = ReloadAimAlpha;

	const float WeaponPoseAlpha = FMath::Clamp(ViewModelWeaponPoseAlpha, 0.0f, 1.0f);
	const float WeaponDetailStart = FMath::Clamp(WeaponDetailAlphaStart, 0.0f, 1.0f);
	const float WeaponDetailEnd = FMath::Max(FMath::Clamp(WeaponDetailAlphaEnd, 0.0f, 1.0f), WeaponDetailStart + KINDA_SMALL_NUMBER);
	const FWeaponValues& WeaponValues = CurrentProceduralValues->WeaponValues;
	const float RawWeaponDetailAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(WeaponDetailStart, WeaponDetailEnd),
		FVector2D(0.0f, 1.0f),
		WeaponPoseAlpha
	);
	const float WeaponDetailAlpha = GetEasedAlpha(RawWeaponDetailAlpha, WeaponValues.WeaponDetailAlphaEaseStrength);
	const float EquipDetailAlpha = FMath::Clamp(ViewModelWeaponEquipDetailAlpha, 0.0f, 1.0f);
	const float SprintExitDetailAlpha = FMath::Clamp(ViewModelSprintExitDetailAlpha, 0.0f, 1.0f);
	const float DetailRecoveryAlpha = EquipDetailAlpha * SprintExitDetailAlpha;
	const float NonSprintProceduralAlpha = FMath::Clamp(ViewModelNonSprintProceduralAlpha, 0.0f, 1.0f) * DetailRecoveryAlpha;

	const bool bUseFirearmProcedural =
		CurrentWeaponType == EWeaponType::Rifle ||
		CurrentWeaponType == EWeaponType::Pistol;
	const bool bUseMeleeProcedural = CurrentWeaponType == EWeaponType::Melee;

	const float FirearmAlpha = bUseFirearmProcedural ? WeaponDetailAlpha : 0.0f;
	const float SharedWeaponAlpha = (bUseFirearmProcedural || bUseMeleeProcedural) ? WeaponDetailAlpha : 0.0f;
	const float FirearmMinorProceduralAlpha = FirearmAlpha * NonSprintProceduralAlpha;
	const float SharedMinorProceduralAlpha = SharedWeaponAlpha * NonSprintProceduralAlpha;
	const float RawSprintAlpha = GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength);
	const float RuntimeSprintAlpha = RawSprintAlpha * FirearmAlpha;
	const float ReloadPoseAlpha = FMath::Clamp(ViewModelReloadPoseAlpha, 0.0f, 1.0f);
	const float EquipPoseAlpha = FMath::Clamp(ViewModelEquipPoseAlpha, 0.0f, 1.0f);
	const float SlidePoseAlpha = FMath::Clamp(ViewModelSlidePoseAlpha, 0.0f, 1.0f);

	const FVector RuntimeSwayLoc = ViewModelSwayLoc * SharedMinorProceduralAlpha;
	const FRotator RuntimeSwayRot = ViewModelSwayRot * SharedMinorProceduralAlpha;
	const FVector RuntimeRecoilLoc = ViewModelRecoilLoc * FirearmMinorProceduralAlpha;
	const FRotator RuntimeRecoilRot = ViewModelRecoilRot * FirearmMinorProceduralAlpha;
	const FVector RuntimeLeftHandRecoilJointTargetLoc =
		RuntimeRecoilLoc * WeaponValues.LeftHandRecoilJointTargetLocScale;
	const FVector RuntimeLeftUpperArmRecoilLoc =
		RuntimeRecoilLoc * WeaponValues.LeftUpperArmRecoilLocScale;
	const FRotator RuntimeLeftUpperArmRecoilRot =
		RuntimeRecoilRot * WeaponValues.LeftUpperArmRecoilRotScale;
	const FVector RuntimeLeftHandSprintPitchJointTargetLoc =
		GetPitchCurveVectorValue(
			WeaponValues.LeftHandSprintPitchJointTargetLocCurve,
			FVector::ZeroVector,
			AimPitch,
			WeaponValues.PitchOffsetMin,
			WeaponValues.PitchOffsetMax
		) * RuntimeSprintAlpha;
	const FVector RuntimeLeftUpperArmSprintPitchLoc =
		GetPitchCurveVectorValue(
			WeaponValues.LeftHandSprintPitchUpperArmLocCurve,
			FVector::ZeroVector,
			AimPitch,
			WeaponValues.PitchOffsetMin,
			WeaponValues.PitchOffsetMax
		) * RuntimeSprintAlpha;
	const FRotator RuntimeLeftUpperArmSprintPitchRot =
		GetPitchCurveRotatorValue(
			WeaponValues.LeftHandSprintPitchUpperArmRotCurve,
			FRotator::ZeroRotator,
			AimPitch,
			WeaponValues.PitchOffsetMin,
			WeaponValues.PitchOffsetMax
		) * RuntimeSprintAlpha;
	const FVector RuntimeForwardWalkLoc = ViewModelForwardWalkLoc * FirearmMinorProceduralAlpha;
	const FRotator RuntimeForwardWalkRot = ViewModelForwardWalkRot * ViewModelWalkAnimAlpha * FirearmMinorProceduralAlpha;
	const float RuntimeForwardWalkAlpha = ViewModelWalkAnimAlpha * FirearmMinorProceduralAlpha;
	const FVector RuntimeJumpLandLoc = ViewModelJumpLandLoc * FirearmMinorProceduralAlpha;
	const FRotator RuntimeJumpLandRot = ViewModelJumpLandRot * FirearmMinorProceduralAlpha;
	const float RuntimeJumpLandAlpha = ViewModelJumpLandAlpha * FirearmMinorProceduralAlpha;

	const FVector RuntimeWeaponRootLocOffset =
		ViewModelHipPoseLoc +
		(ViewModelMovementLoc * RuntimeSprintAlpha) +
		RuntimeRecoilLoc +
		RuntimeSwayLoc +
		(ViewModelForwardWalkLoc * ViewModelWalkAnimAlpha * FirearmMinorProceduralAlpha) +
		(ViewModelForwardWalkAnimLoc * FirearmMinorProceduralAlpha) +
		(ViewModelStartStopLoc * FirearmMinorProceduralAlpha) +
		RuntimeJumpLandLoc;
	const FRotator RuntimeWeaponRootRotOffset = (
		ViewModelHipPoseRot.Quaternion() *
		(ViewModelMovementRot * RuntimeSprintAlpha).Quaternion() *
		RuntimeRecoilRot.Quaternion() *
		RuntimeSwayRot.Quaternion() *
		RuntimeForwardWalkRot.Quaternion() *
		(ViewModelForwardWalkAnimRot * FirearmMinorProceduralAlpha).Quaternion() *
		(ViewModelCurrentStrafeWalkRot * FirearmMinorProceduralAlpha).Quaternion() *
		(ViewModelStartStopRot * FirearmMinorProceduralAlpha).Quaternion() *
		RuntimeJumpLandRot.Quaternion() 
		).Rotator();

	ViewModelProceduralRuntime.WeaponPoseAlpha = WeaponPoseAlpha;
	ViewModelProceduralRuntime.WeaponRootLocOffset = RuntimeWeaponRootLocOffset;
	ViewModelProceduralRuntime.WeaponRootRotOffset = RuntimeWeaponRootRotOffset;

	ViewModelProceduralRuntime.RightHandIKLocOffset = FVector::ZeroVector;
	ViewModelProceduralRuntime.SprintPoseLoc = ViewModelSprintPoseLoc;
	ViewModelProceduralRuntime.SprintPoseRot = ViewModelSprintPoseRot;
	ViewModelProceduralRuntime.SprintAlpha = RuntimeSprintAlpha;
	ViewModelProceduralRuntime.LeftHandIKLoc = ViewModelLeftHandIKLoc;
	ViewModelProceduralRuntime.LeftHandIKRot = ViewModelLeftHandIKRot;
	ViewModelProceduralRuntime.LeftHandGripOffsetLoc = ViewModelLeftHandGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandGripOffsetRot = ViewModelLeftHandGripOffsetRot;
	const float RuntimeLeftHandIKAlpha = bUseFirearmProcedural ? ViewModelLeftHandIKAlpha : 0.0f;
	ViewModelProceduralRuntime.LeftHandIKAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftHandFreeAlpha = bUseFirearmProcedural ? (1.0f - RuntimeLeftHandIKAlpha) : 1.0f;
	ViewModelProceduralRuntime.LeftHandJointTargetLoc =
		ViewModelLeftHandJointTargetLoc +
		(WeaponValues.LeftHandSprintJointTargetLoc * RuntimeSprintAlpha) +
		RuntimeLeftHandSprintPitchJointTargetLoc +
		RuntimeLeftHandRecoilJointTargetLoc;
	ViewModelProceduralRuntime.LeftUpperArmPitchLoc =
		ViewModelLeftUpperArmPitchLoc +
		(WeaponValues.LeftUpperArmSprintLoc * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchLoc +
		RuntimeLeftUpperArmRecoilLoc;
	ViewModelProceduralRuntime.LeftUpperArmPitchRot =
		ViewModelLeftUpperArmPitchRot +
		(WeaponValues.LeftUpperArmSprintRot * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchRot +
		RuntimeLeftUpperArmRecoilRot;
	ViewModelProceduralRuntime.LeftUpperArmPitchAlpha = RuntimeLeftHandIKAlpha;

	// 재장전 시 팔을 앞/아래로 밀어 카메라 클리핑 방지 (reload 알파로 자동 페이드인/아웃)
	ViewModelProceduralRuntime.ReloadPushLoc = WeaponValues.ReloadPushLoc * ReloadPoseAlpha;
	ViewModelProceduralRuntime.ReloadPushRot = WeaponValues.ReloadPushRot * ReloadPoseAlpha;

	if (bDebugLeftHandIK && LeftHandIKDebugLogTime <= 0.0f)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s [FPAnim][LeftHandIK:Runtime] Weapon=%s Type=%d SocketValid=%d RuntimeAlpha=%.3f ViewAlpha=%.3f WeaponPoseAlpha=%.3f DetailAlpha=%.3f ReloadAlpha=%.3f EquipAlpha=%.3f SlideAlpha=%.3f AimPitch=%.2f JointCurve=%s RuntimeLoc=%s RuntimeRot=%s JointTarget=%s PitchJointTarget=%s SprintJointOffset=%s SprintUpperArmLoc=%s SprintUpperArmRot=%s TargetLoc=%s TargetRot=%s"),
			CachedShooterCharacter ? OutlierNet::GetNetPrefix(CachedShooterCharacter) : TEXT("[NoOwner]"),
			*GetNameSafe(CurrentWeapon),
			static_cast<int32>(CurrentWeaponType),
			bDebugLastLeftHandIKSocketValid ? 1 : 0,
			RuntimeLeftHandIKAlpha,
			ViewModelLeftHandIKAlpha,
			WeaponPoseAlpha,
			WeaponDetailAlpha,
			ReloadPoseAlpha,
			EquipPoseAlpha,
			SlidePoseAlpha,
			AimPitch,
			*GetNameSafe(WeaponValues.PitchLeftHandJointTargetLocCurve),
			*ViewModelProceduralRuntime.LeftHandIKLoc.ToCompactString(),
			*ViewModelProceduralRuntime.LeftHandIKRot.ToCompactString(),
			*ViewModelProceduralRuntime.LeftHandJointTargetLoc.ToCompactString(),
			*ViewModelLeftHandJointTargetLoc.ToCompactString(),
			*(WeaponValues.LeftHandSprintJointTargetLoc * RuntimeSprintAlpha).ToCompactString(),
			*(WeaponValues.LeftUpperArmSprintLoc * RuntimeSprintAlpha).ToCompactString(),
			*(WeaponValues.LeftUpperArmSprintRot * RuntimeSprintAlpha).ToCompactString(),
			*DebugLastLeftHandIKTargetLoc.ToCompactString(),
			*DebugLastLeftHandIKTargetRot.ToCompactString());
		LeftHandIKDebugLogTime = FMath::Max(LeftHandIKDebugLogInterval, 0.05f);
	}
	ViewModelProceduralRuntime.MovementLoc = ViewModelMovementLoc;
	ViewModelProceduralRuntime.MovementRot = ViewModelMovementRot;
	ViewModelProceduralRuntime.FingerMovementAlpha = FingerMovementAlpha;
	ViewModelProceduralRuntime.bIsForwardWalk = RuntimeForwardWalkAlpha > KINDA_SMALL_NUMBER;
	ViewModelProceduralRuntime.ForwardWalkLoc = RuntimeForwardWalkLoc;
	ViewModelProceduralRuntime.ForwardWalkRot = RuntimeForwardWalkRot;
	ViewModelProceduralRuntime.ForwardWalkAlpha = RuntimeForwardWalkAlpha;
	ViewModelProceduralRuntime.ForwardWalkAnimLoc = ViewModelForwardWalkAnimLoc * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.ForwardWalkAnimRot = ViewModelForwardWalkAnimRot * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.StrafeWalkAlpha = StrafeWalkAlpha * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.CurrentStrafeWalkRot = ViewModelCurrentStrafeWalkRot * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.ReloadPoseAlpha = FirearmAlpha * ReloadPoseAlpha;
	ViewModelProceduralRuntime.EquipPoseAlpha = SharedWeaponAlpha * EquipPoseAlpha;
	ViewModelProceduralRuntime.SlidePoseAlpha = FirearmAlpha * SlidePoseAlpha;
	ViewModelProceduralRuntime.RecoilLoc = RuntimeRecoilLoc;
	ViewModelProceduralRuntime.RecoilRot = RuntimeRecoilRot;
	ViewModelProceduralRuntime.SwayLoc = RuntimeSwayLoc;
	ViewModelProceduralRuntime.SwayRot = RuntimeSwayRot;
	ViewModelProceduralRuntime.JumpLandLoc = RuntimeJumpLandLoc;
	ViewModelProceduralRuntime.JumpLandRot = RuntimeJumpLandRot;
	ViewModelProceduralRuntime.JumpLandAlpha = RuntimeJumpLandAlpha;
	ViewModelProceduralRuntime.WallOffsetLoc = ViewModelWallOffsetLoc;
	ViewModelProceduralRuntime.WallOffsetRot = ViewModelWallOffsetRot;
	ViewModelProceduralRuntime.WallOffsetAlpha = WallOffsetAlpha;
	ViewModelProceduralRuntime.StartStopLoc = ViewModelStartStopLoc * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.StartStopRot = ViewModelStartStopRot * FirearmMinorProceduralAlpha;
	ViewModelProceduralRuntime.bIsAiming = bIsAiming;
	ViewModelProceduralRuntime.bIsCrouching = bIsCrouching;
	ViewModelProceduralRuntime.bIsReloading = bIsReloading;
}
