// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "DrawDebugHelpers.h"
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
		ViewModelReloadIKBlendAlpha = 0.0f;
		ViewModelEquipIKBlendAlpha = 0.0f;
		ViewModelSlideIKBlendAlpha = 0.0f;
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
	const float EquipIKBlendInSpeed = WeaponValues ? WeaponValues->LeftHandEquipIKBlendInSpeed : ReloadBlendInSpeed;
	const float EquipIKBlendOutSpeed = WeaponValues ? WeaponValues->LeftHandEquipIKBlendOutSpeed : ReloadBlendOutSpeed;
	const float SlideIKBlendInSpeed = WeaponValues ? WeaponValues->LeftHandSlideIKBlendInSpeed : SlideBlendInSpeed;
	const float SlideIKBlendOutSpeed = WeaponValues ? WeaponValues->LeftHandSlideIKBlendOutSpeed : SlideBlendOutSpeed;
	
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

	ViewModelReloadIKBlendAlpha = ViewModelReloadPoseAlpha;

	ViewModelSlidePoseAlpha = FMath::FInterpTo(
		ViewModelSlidePoseAlpha,
		(bIsSliding && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSliding ? SlideBlendInSpeed : SlideBlendOutSpeed
	);

	ViewModelEquipPoseAlpha = FMath::Clamp(1.0f - ViewModelWeaponEquipDetailAlpha, 0.0f, 1.0f) * (bCanUseWeaponPose ? 1.0f : 0.0f);
	ViewModelEquipIKBlendAlpha = FMath::FInterpTo(
		ViewModelEquipIKBlendAlpha,
		ViewModelEquipPoseAlpha,
		DeltaSeconds,
		ViewModelEquipPoseAlpha > ViewModelEquipIKBlendAlpha ? EquipIKBlendInSpeed : EquipIKBlendOutSpeed
	);
	ViewModelSlideIKBlendAlpha = FMath::FInterpTo(
		ViewModelSlideIKBlendAlpha,
		(bIsSliding && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSliding ? SlideIKBlendInSpeed : SlideIKBlendOutSpeed
	);

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

	const FWeaponValues* WeaponValuesForAim = CurrentProceduralValues ? &CurrentProceduralValues->WeaponValues : nullptr;
	const float AimInterpSpeedIn = WeaponValuesForAim ? FMath::Max(WeaponValuesForAim->AimInterpSpeedIn, 0.0f) : 12.0f;
	const float AimInterpSpeedOut = WeaponValuesForAim ? FMath::Max(WeaponValuesForAim->AimInterpSpeedOut, 0.0f) : 12.0f;
	const bool bWantsAim = bIsAiming;
	const bool bWantsReloadAim = bIsAiming && bIsReloading;

	ViewModelAimAlpha = FMath::FInterpTo(
		ViewModelAimAlpha,
		bWantsAim ? 1.0f : 0.0f,
		DeltaSeconds,
		bWantsAim ? AimInterpSpeedIn : AimInterpSpeedOut
	);

	ReloadAimAlpha = FMath::FInterpTo(
		ReloadAimAlpha,
		bWantsReloadAim ? 1.0f : 0.0f,
		DeltaSeconds,
		bWantsReloadAim ? AimInterpSpeedIn : AimInterpSpeedOut
	);

	const bool bWantsWalkAnim =
		bCanUseFirearmProcedural &&
		bShouldMove &&
		!bIsSliding &&
		(bIsSprinting || bNonSprintProceduralReady);

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

	ViewModelJumpLandAlpha = FMath::FInterpTo(
		ViewModelJumpLandAlpha,
		bIsFalling ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsFalling ? 8.0f : 12.0f
	);

	if (CurrentProceduralValues)
	{
		const bool bCanPlayIdleDetail =
			bCanUseFirearmProcedural &&
			!bShouldMove &&
			!bIsSprinting &&
			!bIsSliding &&
			!bIsReloading &&
			!bIsFalling &&
			ViewModelWeaponPoseAlpha > 0.5f;
		const float TargetIdleIntensity = bCanPlayIdleDetail
			? (bIsAiming ? WeaponValues->IdleAimIntensity : WeaponValues->IdleHipIntensity)
			: 0.0f;
		ViewModelIdleIntensity = FMath::FInterpTo(
			ViewModelIdleIntensity,
			TargetIdleIntensity,
			DeltaSeconds,
			WeaponValues->IdleIntensityInterpSpeed
		);

		const float AimLeanBlockAlpha = FMath::Clamp(ViewModelAimAlpha, 0.0f, 1.0f);
		const float LeanYaw = CachedShooterCharacter
			? CachedShooterCharacter->GetCurrentLeanAlpha() * WeaponValues->FirstPersonLeanYawDegrees * (1.0f - AimLeanBlockAlpha)
			: 0.0f;
		ViewModelLeanRot = FMath::RInterpTo(
			ViewModelLeanRot,
			FRotator(0.0f, LeanYaw, 0.0f),
			DeltaSeconds,
			WeaponValues->FirstPersonLeanInterpSpeed
		);
		ViewModelLeanAlpha = 1.0f;
		bIsLean = FMath::Abs(LeanYaw) > KINDA_SMALL_NUMBER;

		const float FingerBaseAlpha = FMath::Clamp(WeaponValues->FingerMovementAlpha, 0.0f, 1.0f);
		if (bCanPlayIdleDetail && FingerBaseAlpha > KINDA_SMALL_NUMBER)
		{
			if (bFingerMovementPulseActive)
			{
				FingerMovementPulseTime = FMath::Max(FingerMovementPulseTime - DeltaSeconds, 0.0f);
				FingerMovementAlpha = FingerBaseAlpha;

				if (FingerMovementPulseTime <= 0.0f)
				{
					bFingerMovementPulseActive = false;
					FingerMovementAlpha = 0.0f;
					FingerMovementCooldownTime = FMath::RandRange(
						FMath::Min(WeaponValues->FingerMovementIntervalMinTime, WeaponValues->FingerMovementIntervalMaxTime),
						FMath::Max(WeaponValues->FingerMovementIntervalMinTime, WeaponValues->FingerMovementIntervalMaxTime)
					);
				}
			}
			else
			{
				FingerMovementCooldownTime -= DeltaSeconds;

				if (FingerMovementCooldownTime <= 0.0f)
				{
					bFingerMovementPulseActive = true;
					FingerMovementPulseTime = FMath::RandRange(
						FMath::Min(WeaponValues->FingerMovementPulseMinTime, WeaponValues->FingerMovementPulseMaxTime),
						FMath::Max(WeaponValues->FingerMovementPulseMinTime, WeaponValues->FingerMovementPulseMaxTime)
					);
					FingerMovementAlpha = FingerBaseAlpha;
				}
				else
				{
					FingerMovementAlpha = 0.0f;
				}
			}
		}
		else
		{
			bFingerMovementPulseActive = false;
			FingerMovementPulseTime = 0.0f;
			FingerMovementCooldownTime = 0.0f;
			FingerMovementAlpha = 0.0f;
		}

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
		ViewModelIdleIntensity = FMath::FInterpTo(ViewModelIdleIntensity, 0.0f, DeltaSeconds, 8.0f);
		ViewModelLeanRot = FMath::RInterpTo(ViewModelLeanRot, FRotator::ZeroRotator, DeltaSeconds, 12.0f);
		ViewModelLeanAlpha = 1.0f;
		bIsLean = false;
		bFingerMovementPulseActive = false;
		FingerMovementPulseTime = 0.0f;
		FingerMovementCooldownTime = 0.0f;
		FingerMovementAlpha = 0.0f;
		ViewModelCurrentStrafeWalkRot = FMath::RInterpTo(ViewModelCurrentStrafeWalkRot, FRotator::ZeroRotator, DeltaSeconds, 8.0f);
		ViewModelStartStopLoc = FVector::ZeroVector;
		ViewModelStartStopRot = FRotator::ZeroRotator;
		StartStopDirection = 0;
		bWasShouldMove = bShouldMove;
	}

	UpdateWallOffset(DeltaSeconds, WeaponValues);
	UpdateFirstPersonProceduralValues(DeltaSeconds);

	UpdateViewModelRecoil(DeltaSeconds);
	UpdateFirstPersonProceduralRuntime();

	bWasSprinting = bIsSprinting;
	bHadWeaponPose = bCanUseWeaponPose;
	bBlockStartStopThisFrame = false;
}

void UShooterFirstPersonAnimInstance::AddViewModelRecoil(float GameplayRecoilScale)
{
	AddViewModelRecoil(GameplayRecoilScale, FVector2D::ZeroVector);
}

void UShooterFirstPersonAnimInstance::AddViewModelRecoil(float GameplayRecoilScale, const FVector2D& NormalizedShotDirection)
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

	const FVector ShotDirectionLoc(
		0.0f,
		NormalizedShotDirection.X * RecoilValues.DirectionLocYInfluence,
		NormalizedShotDirection.Y * RecoilValues.DirectionLocZInfluence
	);
	const FVector ShotDirectionRot(
		NormalizedShotDirection.X * RecoilValues.DirectionRollInfluence,
		NormalizedShotDirection.Y * RecoilValues.DirectionPitchInfluence,
		NormalizedShotDirection.X * RecoilValues.DirectionYawInfluence
	);

	ViewModelRecoilLoc += (RecoilValues.RecoilAmplitudeLoc + RandomLoc + ShotDirectionLoc) * GameplayRecoilScale;
	ViewModelRecoilRot += FRotator::MakeFromEuler(
		(RecoilValues.RecoilAmplitudeRot + RandomRot + ShotDirectionRot) * GameplayRecoilScale
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
	if (!CurrentProceduralValues)
	{
		ViewModelProceduralRuntime = FFirstPersonProceduralAnimRuntime();
		ViewModelHipPoseLoc = FVector::ZeroVector;
		ViewModelHipPoseRot = FRotator::ZeroRotator;
		ViewModelIdleIntensity = 0.0f;
		ViewModelAimPoseLoc = FVector::ZeroVector;
		ViewModelAimPoseRot = FRotator::ZeroRotator;
		ViewModelSprintPoseLoc = FVector::ZeroVector;
		ViewModelSprintPoseRot = FRotator::ZeroRotator;
		ViewModelLeftHandIKLoc = FVector::ZeroVector;
		ViewModelLeftHandIKRot = FRotator::ZeroRotator;
		ViewModelLeftHandIKAlpha = 0.0f;
		ViewModelLeftHandGripOffsetLoc = FVector::ZeroVector;
		ViewModelLeftHandGripOffsetRot = FRotator::ZeroRotator;
		ViewModelWeaponPoseAlpha = 0.0f;
		ViewModelWeaponEquipDetailAlpha = 0.0f;
		ViewModelNonSprintProceduralAlpha = 0.0f;
		ViewModelReloadPoseAlpha = 0.0f;
		ViewModelReloadIKBlendAlpha = 0.0f;
		ViewModelFireIKAlpha = 0.0f;
		ViewModelEquipPoseAlpha = 0.0f;
		ViewModelEquipIKBlendAlpha = 0.0f;
		ViewModelSlidePoseAlpha = 0.0f;
		ViewModelSlideIKBlendAlpha = 0.0f;
		ViewModelSprintExitDetailAlpha = 1.0f;
		SprintExitDetailBlockTimer = 0.0f;
		ViewModelLeftHandJointTargetLoc = FVector::ZeroVector;
		ViewModelStandLeftHandJointTargetLoc = FVector::ZeroVector;
		ViewModelLeftUpperArmPitchLoc = FVector::ZeroVector;
		ViewModelLeftUpperArmPitchRot = FRotator::ZeroRotator;
		ViewModelLeftLowerArmPitchRot = FRotator::ZeroRotator;
		ViewModelStandLeftUpperArmPitchLoc = FVector::ZeroVector;
		ViewModelStandLeftUpperArmPitchRot = FRotator::ZeroRotator;
		ViewModelStandLeftLowerArmPitchRot = FRotator::ZeroRotator;
		ViewModelLeanRot = FRotator::ZeroRotator;
		ViewModelLeanAlpha = 1.0f;
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
		WallOffsetLoc = FVector::ZeroVector;
		WallOffsetRot = FRotator::ZeroRotator;
		WallOffsetAlpha = 0.0f;
		WallTargetAlpha = 0.0f;
		WallAvoidUpPose = nullptr;
		WallAvoidDownPose = nullptr;
		WallAvoidUpAlpha = 0.0f;
		WallAvoidDownAlpha = 0.0f;
		WallAvoidSideAlpha = 0.0f;
		WallAvoidSideSign = 0.0f;
		WallHardStopTargetAlpha = 0.0f;
		WallHardStopSmoothedTargetAlpha = 0.0f;
		WallHardStopAlpha = 0.0f;
		WallHardStopSafetyAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceAlpha = 0.0f;
		WallMuzzleBlockReleaseHoldTimer = 0.0f;
		FingerMovementAlpha = 0.0f;
		FingerMovementPulseTime = 0.0f;
		FingerMovementCooldownTime = 0.0f;
		bFingerMovementPulseActive = false;
		bIsLean = false;
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

	SprintAnimMultiplier = WeaponValues.SprintAnimMultiplier;

	const float PitchSprintAlpha = GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength);
	const float AimPitchScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.AimPitchOffsetScale, 0.0f, 1.0f),
		FMath::Clamp(ViewModelAimAlpha, 0.0f, 1.0f)
	);
	const float ScaledAimPitch = AimPitch * AimPitchScale;
	const float ProceduralAimPitch = bIsSprinting
		? 0.0f
		: FMath::Lerp(ScaledAimPitch, 0.0f, PitchSprintAlpha);
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
	const FRotator StandPitchLeftLowerArmRot = WeaponValues.LeftLowerArmRot;

	ViewModelStandLeftHandJointTargetLoc = StandPitchLeftHandJointTargetLoc;
	ViewModelStandLeftUpperArmPitchLoc = StandPitchLeftUpperArmLoc;
	ViewModelStandLeftUpperArmPitchRot = StandPitchLeftUpperArmRot;
	ViewModelStandLeftLowerArmPitchRot = StandPitchLeftLowerArmRot;

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
	ViewModelLeftHandJointTargetLoc = FMath::Lerp(StandPitchLeftHandJointTargetLoc, CrouchPitchLeftHandJointTargetLoc, PitchCrouchAlpha);
	ViewModelLeftUpperArmPitchLoc = FMath::Lerp(StandPitchLeftUpperArmLoc, CrouchPitchLeftUpperArmLoc, PitchCrouchAlpha);
	ViewModelLeftUpperArmPitchRot = BlendRotatorOffset(StandPitchLeftUpperArmRot, CrouchPitchLeftUpperArmRot, PitchCrouchAlpha);
	ViewModelLeftLowerArmPitchRot = StandPitchLeftLowerArmRot;

	const float ForwardWalkStrengthMultiplier = bIsSprinting ? SprintAnimMultiplier : 1.0f;
	ViewModelForwardWalkLoc = WeaponValues.WalkTiltLoc * ForwardWalkStrengthMultiplier;
	ViewModelForwardWalkRot = WeaponValues.WalkTiltRot * ForwardWalkStrengthMultiplier;

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
	) * ViewModelWalkAnimAlpha * ForwardWalkStrengthMultiplier;

	ViewModelForwardWalkAnimRot = FRotator(
		WeaponValues.WalkAnimRotAmplitude.Pitch * Zr,
		WeaponValues.WalkAnimRotAmplitude.Yaw * Xr,
		WeaponValues.WalkAnimRotAmplitude.Roll * Xr
	) * ViewModelWalkAnimAlpha * ForwardWalkStrengthMultiplier;

	const FRotator CurrentAimRot = CachedShooterCharacter->GetBaseAimRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(PrevAimRot.Yaw, CurrentAimRot.Yaw);
	const float DeltaPitch = FMath::FindDeltaAngleDegrees(PrevAimRot.Pitch, CurrentAimRot.Pitch);

	PrevAimRot = CurrentAimRot;
	ViewModelJumpLandLoc = WeaponValues.JumpLandLoc;
	ViewModelJumpLandRot = WeaponValues.JumpLandRot;

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
	const float LeftHandEquipIKBlendAlpha = FMath::Clamp(ViewModelEquipIKBlendAlpha, 0.0f, 1.0f);
	const float LeftHandSlideIKBlendAlpha = FMath::Clamp(ViewModelSlideIKBlendAlpha, 0.0f, 1.0f);
	const float LeftHandFirePoseAlpha = FMath::Clamp(ViewModelFireIKAlpha, 0.0f, 1.0f);

	const float LeftHandActionIKAlphaScale =
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandEquipIKAlphaScale, 0.0f, 1.0f), LeftHandEquipIKBlendAlpha) *
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandSlideIKAlphaScale, 0.0f, 1.0f), LeftHandSlideIKBlendAlpha) *
		FMath::Lerp(1.0f, FMath::Clamp(WeaponValues.LeftHandFireIKAlphaScale, 0.0f, 1.0f), LeftHandFirePoseAlpha);
	const float LeftHandSprintAnimMultiplier = FMath::Max(WeaponValues.LeftHandSprintAnimMultiplier, 0.0f);
	const float LeftHandRuntimeSprintAlpha =
		GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength) *
		LeftHandFirearmAlpha *
		LeftHandSprintAnimMultiplier;
	float TargetLeftHandIKAlpha = 0.0f;
	FVector TargetLeftHandGripOffsetLoc = FVector::ZeroVector;
	FRotator TargetLeftHandGripOffsetRot = FRotator::ZeroRotator;
	bool bLeftHandIKSocketValid = false;

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
			// Route 1: 왼손 LeftHandIK 소켓을 ik_hand_gun(총손) 로컬로 변환한 정적 그립 오프셋.
			// 소켓과 총손을 같은(지난) 포즈에서 읽으므로 상대값은 정확하다(강체). 그래프에서 실제 ik_hand_gun에 얹는다.
			if (WeaponPivotBoneIndex != INDEX_NONE)
			{
				const FTransform GunHandCompTransform = ArmsMesh->GetBoneTransform(WeaponPivotBoneIndex, FTransform::Identity);

				// 옛 재구성이 손 타깃에 더하던 sprint 튜닝 오프셋을 raw 값으로 소켓에 합쳐 그립 오프셋에 포함.
				// raw를 쓰는 이유: 절차적 회전은 그래프(ik_hand_gun)가 적용하므로 여기서 또 회전시키면 이중 적용된다.
				// sprint가 아닐 땐 LeftHandRuntimeSprintAlpha=0이라 sprint 항은 0 → 기존 동작 그대로.
				const FVector SprintIKLocRaw = WeaponValues.LeftHandSprintIKLocOffset * LeftHandRuntimeSprintAlpha;
				const FRotator SprintIKRotRaw = WeaponValues.LeftHandSprintIKRotOffset * LeftHandRuntimeSprintAlpha;
				const FQuat SocketLocalQuat = SocketLocalRot.Quaternion();
				const FVector BaseGripSocketLocRaw =
					SocketLocalQuat.RotateVector(WeaponValues.LeftHandGripSocketLocOffset);
				const float WallVeryCloseSocketOffsetAlpha = FMath::Clamp(
					FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f) *
					WeaponValues.LeftHandWallVeryCloseSocketOffsetAlphaScale,
					0.0f,
					1.0f
				);
				const float WallMuzzleBlockSocketOffsetAlpha = FMath::Clamp(
					FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockAlpha, 2.0f) *
					(1.0f - FMath::Clamp(WallMuzzleBlockDownPreferenceAlpha, 0.0f, 1.0f)) *
					(1.0f - WallVeryCloseSocketOffsetAlpha) *
					WeaponValues.LeftHandWallMuzzleBlockSocketOffsetAlphaScale,
					0.0f,
					1.0f
				);
				const FVector WallMuzzleBlockSocketLocRaw =
					SocketLocalQuat.RotateVector(WeaponValues.LeftHandWallMuzzleBlockSocketLocOffset) * WallMuzzleBlockSocketOffsetAlpha;
				const FRotator WallMuzzleBlockSocketRotRaw =
					WeaponValues.LeftHandWallMuzzleBlockSocketRotOffset * WallMuzzleBlockSocketOffsetAlpha;
				const FVector WallVeryCloseSocketLocRaw =
					SocketLocalQuat.RotateVector(WeaponValues.LeftHandWallVeryCloseSocketLocOffset) * WallVeryCloseSocketOffsetAlpha;
				const FRotator WallVeryCloseSocketRotRaw =
					WeaponValues.LeftHandWallVeryCloseSocketRotOffset * WallVeryCloseSocketOffsetAlpha;

				const FVector SocketLocWithOffsets =
					SocketLocalLoc +
					BaseGripSocketLocRaw +
					SprintIKLocRaw +
					WallMuzzleBlockSocketLocRaw +
					WallVeryCloseSocketLocRaw;
				const FQuat SocketRotWithOffsets = (
					SocketLocalQuat *
					WeaponValues.LeftHandGripSocketRotOffset.Quaternion() *
					SprintIKRotRaw.Quaternion() *
					WallMuzzleBlockSocketRotRaw.Quaternion() *
					WallVeryCloseSocketRotRaw.Quaternion()
				).GetNormalized();
				const FTransform SocketCompTransform(SocketRotWithOffsets, SocketLocWithOffsets);
				const FTransform GripOffset = SocketCompTransform.GetRelativeTransform(GunHandCompTransform);
				TargetLeftHandGripOffsetLoc = GripOffset.GetLocation();
				TargetLeftHandGripOffsetRot = GripOffset.Rotator();
			}

			TargetLeftHandIKAlpha = LeftHandFirearmAlpha * LeftHandActionIKAlphaScale;
		}
	}

	ViewModelLeftHandIKLoc = FVector::ZeroVector;
	ViewModelLeftHandIKRot = FRotator::ZeroRotator;
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

	const FWeaponValues& WeaponValues = CurrentProceduralValues->WeaponValues;
	const float RawSprintAlphaForAim = GetEasedAlpha(ViewModelSprintAlpha, WeaponValues.SprintAlphaEaseStrength);
	const float SprintToAimGateAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(WeaponValues.SprintToAimGateStartSprintAlpha, WeaponValues.SprintToAimGateEndSprintAlpha),
		FVector2D(0.0f, 1.0f),
		RawSprintAlphaForAim
	);
	const float RuntimeAimAlpha = ViewModelAimAlpha * SprintToAimGateAlpha;
	const float RuntimeReloadAimAlpha = ReloadAimAlpha * SprintToAimGateAlpha;
	const float AimToSprintGateAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(WeaponValues.AimToSprintGateStartAimAlpha, WeaponValues.AimToSprintGateEndAimAlpha),
		FVector2D(0.0f, 1.0f),
		FMath::Clamp(ViewModelAimAlpha, 0.0f, 1.0f)
	);
	const float ClampedAimAlpha = FMath::Clamp(RuntimeAimAlpha, 0.0f, 1.0f);
	const float AimMidpointAlpha = (ViewModelLeftHandIKAlpha > KINDA_SMALL_NUMBER)
		? 0.0f
		: FMath::Sin(ClampedAimAlpha * PI);
	const FVector AimMidpointOffset = bIsAiming ? WeaponValues.AimMidpointOffsetIn : WeaponValues.AimMidpointOffsetOut;
	const FVector RuntimeAimPoseLoc = ViewModelAimPoseLoc + (AimMidpointOffset * AimMidpointAlpha);
	const FRotator RuntimeAimPoseRot = ViewModelAimPoseRot;

	ViewModelProceduralRuntime.HipPoseLoc = ViewModelHipPoseLoc;
	ViewModelProceduralRuntime.HipPoseRot = ViewModelHipPoseRot;
	ViewModelProceduralRuntime.AimPoseLoc = RuntimeAimPoseLoc;
	ViewModelProceduralRuntime.AimPoseRot = RuntimeAimPoseRot;
	ViewModelProceduralRuntime.AimAlpha = RuntimeAimAlpha;
	ViewModelProceduralRuntime.ReloadAimAlpha = RuntimeReloadAimAlpha;

	ViewModelProceduralRuntime.IdleIntensity = ViewModelIdleIntensity;
	ViewModelProceduralRuntime.LeanRot = ViewModelLeanRot;
	ViewModelProceduralRuntime.LeanAlpha = ViewModelLeanAlpha;

	const float WeaponPoseAlpha = FMath::Clamp(ViewModelWeaponPoseAlpha, 0.0f, 1.0f);
	const float WeaponDetailStart = FMath::Clamp(WeaponDetailAlphaStart, 0.0f, 1.0f);
	const float WeaponDetailEnd = FMath::Max(FMath::Clamp(WeaponDetailAlphaEnd, 0.0f, 1.0f), WeaponDetailStart + KINDA_SMALL_NUMBER);
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
	const float RawSprintAlpha = RawSprintAlphaForAim;
	const float ReloadPoseAlpha = FMath::Clamp(ViewModelReloadPoseAlpha, 0.0f, 1.0f);
	const float ReloadCrossfadeAlpha = ReloadPoseAlpha;
	const float NonReloadCrossfadeAlpha = 1.0f - ReloadCrossfadeAlpha;
	const float EquipPoseAlpha = FMath::Clamp(ViewModelEquipPoseAlpha, 0.0f, 1.0f);
	const float SlidePoseAlpha = FMath::Clamp(ViewModelSlidePoseAlpha, 0.0f, 1.0f);
	const float RuntimeSprintAlpha = RawSprintAlpha * FirearmAlpha * AimToSprintGateAlpha;
	const float RuntimeAimSwayScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.AimSwayScale, 0.0f, 1.0f),
		ClampedAimAlpha
	);
	const float RuntimeAimMovementProceduralScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.AimMovementProceduralScale, 0.0f, 1.0f),
		FMath::Clamp(ViewModelAimAlpha, 0.0f, 1.0f)
	);
	const float NonSprintMovementProceduralAlpha = FirearmMinorProceduralAlpha * RuntimeAimMovementProceduralScale;

	const FVector RuntimeSwayLoc = ViewModelSwayLoc * SharedMinorProceduralAlpha * RuntimeAimSwayScale;
	const FRotator RuntimeSwayRot = ViewModelSwayRot * SharedMinorProceduralAlpha * RuntimeAimSwayScale;
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
	const float ForwardWalkProceduralAlpha = bIsSprinting ? RuntimeSprintAlpha : NonSprintMovementProceduralAlpha;
	const FVector RuntimeForwardWalkLoc = ViewModelForwardWalkLoc * ForwardWalkProceduralAlpha;
	const FRotator RuntimeForwardWalkRot = ViewModelForwardWalkRot * ViewModelWalkAnimAlpha * ForwardWalkProceduralAlpha;
	const float RuntimeForwardWalkAlpha = ViewModelWalkAnimAlpha * ForwardWalkProceduralAlpha;
	const FVector RuntimeJumpLandLoc = ViewModelJumpLandLoc * FirearmMinorProceduralAlpha;
	const FRotator RuntimeJumpLandRot = ViewModelJumpLandRot * FirearmMinorProceduralAlpha;
	const float RuntimeJumpLandAlpha = ViewModelJumpLandAlpha * FirearmMinorProceduralAlpha;

	const FVector RuntimeWeaponRootLocOffset =
		ViewModelHipPoseLoc +
		(ViewModelMovementLoc * RuntimeSprintAlpha) +
		RuntimeRecoilLoc +
		RuntimeSwayLoc +
		(ViewModelForwardWalkLoc * ViewModelWalkAnimAlpha * ForwardWalkProceduralAlpha) +
		(ViewModelForwardWalkAnimLoc * ForwardWalkProceduralAlpha) +
		(ViewModelStartStopLoc * NonSprintMovementProceduralAlpha) +
		RuntimeJumpLandLoc;
	const FRotator RuntimeWeaponRootRotOffset = (
		ViewModelHipPoseRot.Quaternion() *
		(ViewModelMovementRot * RuntimeSprintAlpha).Quaternion() *
		RuntimeRecoilRot.Quaternion() *
		RuntimeSwayRot.Quaternion() *
		RuntimeForwardWalkRot.Quaternion() *
		(ViewModelForwardWalkAnimRot * ForwardWalkProceduralAlpha).Quaternion() *
		(ViewModelCurrentStrafeWalkRot * NonSprintMovementProceduralAlpha).Quaternion() *
		(ViewModelStartStopRot * NonSprintMovementProceduralAlpha).Quaternion() *
		RuntimeJumpLandRot.Quaternion() 
		).Rotator();

	ViewModelProceduralRuntime.WeaponPoseAlpha = WeaponPoseAlpha;
	ViewModelProceduralRuntime.WeaponRootLocOffset = RuntimeWeaponRootLocOffset;
	ViewModelProceduralRuntime.WeaponRootRotOffset = RuntimeWeaponRootRotOffset;

	ViewModelProceduralRuntime.RightHandIKLocOffset = WeaponValues.RightHandReloadIKLocOffset;
	ViewModelProceduralRuntime.RightHandIKRotOffset = WeaponValues.RightHandReloadIKRotOffset;
	ViewModelProceduralRuntime.SprintPoseLoc = ViewModelSprintPoseLoc;
	ViewModelProceduralRuntime.SprintPoseRot = ViewModelSprintPoseRot;
	ViewModelProceduralRuntime.SprintAlpha = RuntimeSprintAlpha;
	const float WallPoseBlendAlpha = FMath::Clamp(FMath::Max3(WallAvoidUpAlpha, WallAvoidDownAlpha, WallMuzzleBlockAlpha), 0.0f, 1.0f);
	const float WallSideSuppressAlpha = FMath::Clamp(FMath::Max3(WallPoseBlendAlpha, WallVeryCloseAlpha, WallCeilingAlpha), 0.0f, 1.0f);
	const float WallSideOnlyAlpha = FMath::Clamp(WallAvoidSideAlpha * (1.0f - WallSideSuppressAlpha), 0.0f, 1.0f);
	const float WallVeryClosePoseSuppressAlpha = FMath::Clamp(
		FMath::Max(
			FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f) *
			WeaponValues.WallVeryClosePoseSuppressScale,
			FMath::Max(WallHardStopAlpha, WallHardStopSafetyAlpha)
		),
		0.0f,
		1.0f
	);
	const float WallMuzzleBlockOnlyAlpha = FMath::Clamp(WallMuzzleBlockAlpha * (1.0f - WallVeryClosePoseSuppressAlpha), 0.0f, 1.0f);
	const float WallMuzzleBlockArmAlpha = FMath::Clamp(
		FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockOnlyAlpha, 2.0f) *
		WeaponValues.LeftHandWallMuzzleBlockArmAlphaScale,
		0.0f,
		1.0f
	);
	ViewModelProceduralRuntime.LeftHandIKLoc = ViewModelLeftHandIKLoc;
	ViewModelProceduralRuntime.LeftHandIKRot = ViewModelLeftHandIKRot;
	ViewModelProceduralRuntime.LeftHandGripOffsetLoc =
		ViewModelLeftHandGripOffsetLoc +
		(WeaponValues.LeftHandWallSideIKLocOffset * WallSideOnlyAlpha * WallAvoidSideSign);
	ViewModelProceduralRuntime.LeftHandGripOffsetRot = ViewModelLeftHandGripOffsetRot;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetLoc = WeaponValues.LeftHandReloadGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetRot = WeaponValues.LeftHandReloadGripOffsetRot;
	const float RuntimeLeftHandIKAlpha = bUseFirearmProcedural
		? ViewModelLeftHandIKAlpha * NonReloadCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandReloadIKAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandReloadIKAlpha, 0.0f, 1.0f) * ReloadCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandReloadArmAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandReloadArmAlpha, 0.0f, 1.0f) * ReloadCrossfadeAlpha
		: 0.0f;
	ViewModelProceduralRuntime.LeftHandIKAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftHandFreeAlpha = bUseFirearmProcedural ? (1.0f - RuntimeLeftHandIKAlpha) : 1.0f;
	const FVector RuntimeLeftHandJointTargetLoc =
		ViewModelLeftHandJointTargetLoc +
		(WeaponValues.LeftHandSprintJointTargetLoc * RuntimeSprintAlpha) +
		RuntimeLeftHandSprintPitchJointTargetLoc +
		RuntimeLeftHandRecoilJointTargetLoc +
		(WeaponValues.LeftHandWallMuzzleBlockJointTargetLoc * WallMuzzleBlockArmAlpha);
	ViewModelProceduralRuntime.LeftHandJointTargetLoc = RuntimeLeftHandJointTargetLoc;
	ViewModelProceduralRuntime.LeftHandReloadIKLoc = WeaponValues.LeftHandReloadIKLoc * ReloadCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadIKRot = WeaponValues.LeftHandReloadIKRot * ReloadCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadJointTargetLoc =
		ViewModelStandLeftHandJointTargetLoc +
		(WeaponValues.LeftHandReloadJointTargetLoc * RuntimeLeftHandReloadArmAlpha);
	ViewModelProceduralRuntime.LeftHandReloadIKAlpha = RuntimeLeftHandReloadIKAlpha;
	ViewModelProceduralRuntime.LeftUpperArmPitchLoc =
		ViewModelLeftUpperArmPitchLoc +
		(WeaponValues.LeftUpperArmSprintLoc * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchLoc +
		RuntimeLeftUpperArmRecoilLoc +
		(WeaponValues.LeftUpperArmWallMuzzleBlockLoc * WallMuzzleBlockArmAlpha);
	ViewModelProceduralRuntime.LeftUpperArmPitchRot =
		ViewModelLeftUpperArmPitchRot +
		(WeaponValues.LeftUpperArmSprintRot * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchRot +
		RuntimeLeftUpperArmRecoilRot +
		(WeaponValues.LeftUpperArmWallMuzzleBlockRot * WallMuzzleBlockArmAlpha);
	ViewModelProceduralRuntime.LeftUpperArmPitchAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftLowerArmPitchRot =
		ViewModelLeftLowerArmPitchRot +
		(WeaponValues.LeftLowerArmWallMuzzleBlockRot * WallMuzzleBlockArmAlpha);
	ViewModelProceduralRuntime.LeftLowerArmPitchAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftUpperArmReloadLoc =
		ViewModelStandLeftUpperArmPitchLoc +
		(WeaponValues.LeftUpperArmReloadLoc * RuntimeLeftHandReloadArmAlpha);
	ViewModelProceduralRuntime.LeftUpperArmReloadRot = BlendRotatorOffset(
		ViewModelStandLeftUpperArmPitchRot,
		ViewModelStandLeftUpperArmPitchRot + WeaponValues.LeftUpperArmReloadRot,
		RuntimeLeftHandReloadArmAlpha
	);
	ViewModelProceduralRuntime.LeftLowerArmReloadRot = BlendRotatorOffset(
		ViewModelStandLeftLowerArmPitchRot,
		ViewModelStandLeftLowerArmPitchRot + WeaponValues.LeftLowerArmReloadRot,
		RuntimeLeftHandReloadArmAlpha
	);

	// 재장전 시 팔을 앞/아래로 밀어 카메라 클리핑 방지 (reload 알파로 자동 페이드인/아웃)
	ViewModelProceduralRuntime.ReloadPushLoc = WeaponValues.ReloadPushLoc;
	ViewModelProceduralRuntime.ReloadPushRot = WeaponValues.ReloadPushRot;

	ViewModelProceduralRuntime.MovementLoc = ViewModelMovementLoc;
	ViewModelProceduralRuntime.MovementRot = ViewModelMovementRot;
	ViewModelProceduralRuntime.FingerMovementAlpha = FingerMovementAlpha;
	ViewModelProceduralRuntime.bIsForwardWalk = RuntimeForwardWalkAlpha > KINDA_SMALL_NUMBER;
	ViewModelProceduralRuntime.ForwardWalkLoc = RuntimeForwardWalkLoc;
	ViewModelProceduralRuntime.ForwardWalkRot = RuntimeForwardWalkRot;
	ViewModelProceduralRuntime.ForwardWalkAlpha = RuntimeForwardWalkAlpha;
	ViewModelProceduralRuntime.ForwardWalkAnimLoc = ViewModelForwardWalkAnimLoc * ForwardWalkProceduralAlpha;
	ViewModelProceduralRuntime.ForwardWalkAnimRot = ViewModelForwardWalkAnimRot * ForwardWalkProceduralAlpha;
	ViewModelProceduralRuntime.StrafeWalkAlpha = StrafeWalkAlpha * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.CurrentStrafeWalkRot = ViewModelCurrentStrafeWalkRot * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.ReloadPoseAlpha = FirearmAlpha * ReloadCrossfadeAlpha;
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
	ViewModelProceduralRuntime.WallMuzzleBlockAlpha = WallMuzzleBlockOnlyAlpha;
	WallAvoidUpPose = WeaponValues.WallAvoidUpPose;
	WallAvoidDownPose = WeaponValues.WallAvoidDownPose;
	WallOffsetLoc = ViewModelWallOffsetLoc;
	WallOffsetRot = ViewModelWallOffsetRot;
	ViewModelProceduralRuntime.WallAvoidUpPose = WeaponValues.WallAvoidUpPose;
	ViewModelProceduralRuntime.WallAvoidDownPose = WeaponValues.WallAvoidDownPose;
	const float MuzzleBlockDownPoseAlpha =
		WallMuzzleBlockOnlyAlpha *
		WeaponValues.WallMuzzleBlockDownPoseScale *
		FMath::Clamp(WallMuzzleBlockDownPreferenceAlpha, 0.0f, 1.0f);
	const float MuzzleBlockUpPoseAlpha =
		WallMuzzleBlockOnlyAlpha *
		WeaponValues.WallMuzzleBlockUpPoseScale *
		(1.0f - FMath::Clamp(WallMuzzleBlockDownPreferenceAlpha, 0.0f, 1.0f));
	const float RuntimeWallAvoidUpAlpha = FMath::Clamp(
		FMath::Max(WallAvoidUpAlpha, MuzzleBlockUpPoseAlpha) *
		(1.0f - FMath::Clamp(WallCeilingAlpha, 0.0f, 1.0f)) *
		(1.0f - WallVeryClosePoseSuppressAlpha) *
		WeaponValues.WallAvoidUpPoseAlphaScale,
		0.0f,
		1.0f
	);
	ViewModelProceduralRuntime.WallAvoidUpAlpha = RuntimeWallAvoidUpAlpha;
	ViewModelProceduralRuntime.WallAvoidDownAlpha = FMath::Clamp(
		FMath::Max3(
			WallAvoidDownAlpha * (1.0f - WallVeryClosePoseSuppressAlpha),
			MuzzleBlockDownPoseAlpha,
			WallCeilingAlpha * (1.0f - WallVeryClosePoseSuppressAlpha)
		),
		0.0f,
		1.0f
	);
	ViewModelProceduralRuntime.WallAvoidSideAlpha = WallSideOnlyAlpha;
	ViewModelProceduralRuntime.WallAvoidSideSign = WallAvoidSideSign;
	ViewModelProceduralRuntime.StartStopLoc = ViewModelStartStopLoc * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.StartStopRot = ViewModelStartStopRot * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.bIsAiming = bIsAiming;
	ViewModelProceduralRuntime.bIsCrouching = bIsCrouching;
	ViewModelProceduralRuntime.bIsReloading = bIsReloading;
}

void UShooterFirstPersonAnimInstance::UpdateWallOffset(float DeltaSeconds, const FWeaponValues* WeaponValues)
{
	WallTargetAlpha = 0.0f;
	WallUpTargetAlpha = 0.0f;
	WallDownTargetAlpha = 0.0f;
	WallSideTargetAlpha = 0.0f;
	WallSideTargetSign = 0.0f;
	WallMuzzleBlockTargetAlpha = 0.0f;
	WallVeryCloseTargetAlpha = 0.0f;
	WallTopEdgeTargetAlpha = 0.0f;
	WallCeilingTargetAlpha = 0.0f;
	WallHardStopTargetAlpha = 0.0f;
	WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
	WallMuzzleBlockReleaseHoldTimer = FMath::Max(0.0f, WallMuzzleBlockReleaseHoldTimer - DeltaSeconds);

	if (!WeaponValues ||
		!CachedShooterCharacter ||
		!CachedShooterCharacter->IsLocallyControlled() ||
		!CachedShooterCharacter->GetWorld())
	{
		WallOffsetAlpha = 0.0f;
		WallAvoidUpAlpha = 0.0f;
		WallAvoidDownAlpha = 0.0f;
		WallAvoidSideAlpha = 0.0f;
		WallAvoidSideSign = 0.0f;
		WallMuzzleBlockAlpha = 0.0f;
		WallVeryCloseAlpha = 0.0f;
		WallVeryCloseTargetAlpha = 0.0f;
		WallTopEdgeAlpha = 0.0f;
		WallTopEdgeTargetAlpha = 0.0f;
		WallCeilingAlpha = 0.0f;
		WallCeilingTargetAlpha = 0.0f;
		WallHardStopAlpha = 0.0f;
		WallHardStopTargetAlpha = 0.0f;
		WallHardStopSmoothedTargetAlpha = 0.0f;
		WallHardStopSafetyAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceAlpha = 0.0f;
		WallMuzzleBlockReleaseHoldTimer = 0.0f;
		WallAvoidUpPose = nullptr;
		WallAvoidDownPose = nullptr;
		WallOffsetLoc = FVector::ZeroVector;
		WallOffsetRot = FRotator::ZeroRotator;
		ViewModelWallOffsetLoc = FVector::ZeroVector;
		ViewModelWallOffsetRot = FRotator::ZeroRotator;
		return;
	}

	UCameraComponent* Camera = CachedShooterCharacter->GetFirstPersonCameraComponent();
	if (!Camera)
	{
		WallOffsetAlpha = 0.0f;
		WallAvoidUpAlpha = 0.0f;
		WallAvoidDownAlpha = 0.0f;
		WallAvoidSideAlpha = 0.0f;
		WallAvoidSideSign = 0.0f;
		WallMuzzleBlockAlpha = 0.0f;
		WallVeryCloseAlpha = 0.0f;
		WallVeryCloseTargetAlpha = 0.0f;
		WallTopEdgeAlpha = 0.0f;
		WallTopEdgeTargetAlpha = 0.0f;
		WallCeilingAlpha = 0.0f;
		WallCeilingTargetAlpha = 0.0f;
		WallHardStopAlpha = 0.0f;
		WallHardStopTargetAlpha = 0.0f;
		WallHardStopSmoothedTargetAlpha = 0.0f;
		WallHardStopSafetyAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
		WallMuzzleBlockDownPreferenceAlpha = 0.0f;
		WallMuzzleBlockReleaseHoldTimer = 0.0f;
		WallOffsetLoc = FVector::ZeroVector;
		WallOffsetRot = FRotator::ZeroRotator;
		ViewModelWallOffsetLoc = FVector::ZeroVector;
		ViewModelWallOffsetRot = FRotator::ZeroRotator;
		return;
	}

	WallAvoidUpPose = WeaponValues->WallAvoidUpPose;
	WallAvoidDownPose = WeaponValues->WallAvoidDownPose;

	const FVector Start = Camera->GetComponentLocation();
	const FVector Forward = Camera->GetForwardVector();
	const FVector Right = Camera->GetRightVector();
	const FVector Up = Camera->GetUpVector();
	const float TraceDistance = FMath::Max(WeaponValues->WallTraceDistance, 0.0f);
	const float TraceRadius = FMath::Max(WeaponValues->WallTraceRadius, KINDA_SMALL_NUMBER);
	const float SafeDistance = FMath::Max(WeaponValues->WallSafeDistance, KINDA_SMALL_NUMBER);
	const float ProbeForwardOffset = FMath::Max(WeaponValues->WallProbeForwardOffset, 0.0f);
	const float ProbeRightOffset = WeaponValues->WallProbeRightOffset;
	const float ProbeUpOffset = WeaponValues->WallProbeUpOffset;
	const float VerticalProbeOffset = FMath::Max(WeaponValues->WallVerticalProbeOffset, 0.0f);
	const float SideProbeOffset = FMath::Max(WeaponValues->WallSideProbeOffset, TraceRadius * 1.5f);
	const float MuzzleBlockTraceDistance = FMath::Max(WeaponValues->WallMuzzleBlockTraceDistance, 0.0f);
	const float MuzzleBlockSafeDistance = FMath::Max(WeaponValues->WallMuzzleBlockSafeDistance, KINDA_SMALL_NUMBER);
	const float BarrelBlockSafeDistance = FMath::Max(WeaponValues->WallBarrelBlockSafeDistance, KINDA_SMALL_NUMBER);
	const float PitchBlockAlpha = FMath::Clamp(FMath::Abs(Forward.Z), 0.0f, 1.0f);
	const float MuzzleBlockTraceRadius = FMath::Max(
		WeaponValues->WallMuzzleBlockTraceRadius + WeaponValues->WallPitchBlockTraceRadiusBoost * PitchBlockAlpha,
		KINDA_SMALL_NUMBER
	);
	const float BarrelBlockLength = FMath::Max(
		WeaponValues->WallBarrelBlockLength + WeaponValues->WallPitchBlockLengthBoost * PitchBlockAlpha,
		0.0f
	);
	const float BarrelBlockTraceRadius = FMath::Max(
		WeaponValues->WallBarrelBlockTraceRadius + WeaponValues->WallPitchBlockTraceRadiusBoost * PitchBlockAlpha,
		KINDA_SMALL_NUMBER
	);
	const float DebugPointSize = 5.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ViewModelWallTrace), false, CachedShooterCharacter);
	Params.AddIgnoredActor(CachedShooterCharacter);
	if (CurrentWeapon)
	{
		Params.AddIgnoredActor(CurrentWeapon);
	}

	const FVector BaseTraceStart =
		Start +
		Forward * ProbeForwardOffset +
		Right * ProbeRightOffset +
		Up * ProbeUpOffset;

	struct FWallProbeResult
	{
		float Alpha = 0.0f;
		FHitResult Hit;
		bool bHit = false;
	};

	const auto SweepWallProbe = [&](const FVector& ProbeStart) -> FWallProbeResult
	{
		FWallProbeResult Result;
		const FVector ProbeEnd = ProbeStart + Forward * TraceDistance;
		Result.bHit = CachedShooterCharacter->GetWorld()->SweepSingleByChannel(
			Result.Hit,
			ProbeStart,
			ProbeEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(TraceRadius),
			Params
		);

		if (Result.bHit)
		{
			Result.Alpha = FMath::Clamp((TraceDistance - Result.Hit.Distance) / SafeDistance, 0.0f, 1.0f);
		}

		return Result;
	};

	const FWallProbeResult CenterProbe = SweepWallProbe(BaseTraceStart);
	const FWallProbeResult UpperProbe = SweepWallProbe(BaseTraceStart + Up * VerticalProbeOffset);
	const FWallProbeResult LowerProbe = SweepWallProbe(BaseTraceStart - Up * VerticalProbeOffset);
	const FWallProbeResult RightProbe = SweepWallProbe(BaseTraceStart + Right * SideProbeOffset);
	const FWallProbeResult LeftProbe = SweepWallProbe(BaseTraceStart - Right * SideProbeOffset);
	WallTopEdgeTargetAlpha = FMath::Clamp(
		LowerProbe.Alpha - FMath::Max(CenterProbe.Alpha, UpperProbe.Alpha),
		0.0f,
		1.0f
	);
	WallTopEdgeAlpha = FMath::FInterpTo(
		WallTopEdgeAlpha,
		WallTopEdgeTargetAlpha,
		DeltaSeconds,
		WallTopEdgeTargetAlpha > WallTopEdgeAlpha
			? WeaponValues->WallTopEdgeBlendInSpeed
			: WeaponValues->WallTopEdgeBlendOutSpeed
	);
	WallCeilingTargetAlpha = FMath::Clamp(
		UpperProbe.Alpha - FMath::Max(CenterProbe.Alpha, LowerProbe.Alpha),
		0.0f,
		1.0f
	);
	WallCeilingAlpha = FMath::FInterpTo(
		WallCeilingAlpha,
		WallCeilingTargetAlpha,
		DeltaSeconds,
		WallCeilingTargetAlpha > WallCeilingAlpha
			? WeaponValues->WallCeilingBlendInSpeed
			: WeaponValues->WallCeilingBlendOutSpeed
	);

	const FVector MuzzleBlockTraceStart =
		Start +
		Forward * FMath::Max(WeaponValues->WallMuzzleBlockForwardOffset, 0.0f) +
		Right * WeaponValues->WallMuzzleBlockRightOffset +
		Up * WeaponValues->WallMuzzleBlockUpOffset;
	const FVector MuzzleBlockTraceForward = Forward;
	FVector ActualMuzzleLocation = FVector::ZeroVector;
	FVector ActualMuzzleForward = Forward;
	bool bHasActualMuzzleSocket = false;
	if (CurrentWeapon)
	{
		if (USkeletalMeshComponent* FirstPersonWeaponMesh = CurrentWeapon->GetFirstPersonWeaponMesh())
		{
			static const FName MuzzleSocketName(TEXT("Muzzle"));
			if (FirstPersonWeaponMesh->DoesSocketExist(MuzzleSocketName))
			{
				const FTransform MuzzleSocketTransform = FirstPersonWeaponMesh->GetSocketTransform(MuzzleSocketName, RTS_World);
				ActualMuzzleLocation = MuzzleSocketTransform.GetLocation();
				ActualMuzzleForward = MuzzleSocketTransform.GetRotation().GetForwardVector();
				bHasActualMuzzleSocket = true;
			}
		}
	}

	FWallProbeResult MuzzleBlockProbe;
	const FVector MuzzleBlockTraceEnd = MuzzleBlockTraceStart + MuzzleBlockTraceForward * MuzzleBlockTraceDistance;
	if (MuzzleBlockTraceDistance > KINDA_SMALL_NUMBER)
	{
		MuzzleBlockProbe.bHit = CachedShooterCharacter->GetWorld()->SweepSingleByChannel(
			MuzzleBlockProbe.Hit,
			MuzzleBlockTraceStart,
			MuzzleBlockTraceEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(MuzzleBlockTraceRadius),
			Params
		);

		if (MuzzleBlockProbe.bHit)
		{
			MuzzleBlockProbe.Alpha = FMath::Clamp(
				(MuzzleBlockTraceDistance - MuzzleBlockProbe.Hit.Distance) / MuzzleBlockSafeDistance,
				0.0f,
				1.0f
			);
		}
	}
	WallMuzzleBlockTargetAlpha = MuzzleBlockProbe.Alpha;

	FWallProbeResult BarrelBlockProbe;
	const FVector BarrelBlockTraceStart = MuzzleBlockTraceStart - MuzzleBlockTraceForward * BarrelBlockLength;
	const FVector BarrelBlockTraceEnd = MuzzleBlockTraceStart;
	if (BarrelBlockLength > KINDA_SMALL_NUMBER)
	{
		BarrelBlockProbe.bHit = CachedShooterCharacter->GetWorld()->SweepSingleByChannel(
			BarrelBlockProbe.Hit,
			BarrelBlockTraceStart,
			BarrelBlockTraceEnd,
			FQuat::Identity,
			ECC_Visibility,
			FCollisionShape::MakeSphere(BarrelBlockTraceRadius),
			Params
		);

		if (BarrelBlockProbe.bHit)
		{
			BarrelBlockProbe.Alpha = FMath::Clamp(
				(BarrelBlockLength - BarrelBlockProbe.Hit.Distance) / BarrelBlockSafeDistance,
				0.0f,
				1.0f
			);
		}
	}
	const float TopEdgeBarrelBlockScale = FMath::Clamp(WeaponValues->WallTopEdgeBarrelBlockScale, 0.0f, 1.0f);
	const float BarrelBlockTargetAlpha = FMath::Lerp(
		BarrelBlockProbe.Alpha,
		BarrelBlockProbe.Alpha * TopEdgeBarrelBlockScale,
		WallTopEdgeAlpha
	);
	WallMuzzleBlockTargetAlpha = FMath::Max(WallMuzzleBlockTargetAlpha, BarrelBlockTargetAlpha);
	const float HardStopClearance = FMath::Max(WeaponValues->WallHardStopClearance, 0.0f);
	const float HardStopRange = FMath::Max(WeaponValues->WallHardStopRange, KINDA_SMALL_NUMBER);
	const auto GetHardStopAlphaFromDistance = [&](const float Distance) -> float
	{
		return FMath::Clamp((HardStopClearance + HardStopRange - Distance) / HardStopRange, 0.0f, 1.0f);
	};
	const float MuzzleHardStopAlpha = MuzzleBlockProbe.bHit
		? GetHardStopAlphaFromDistance(MuzzleBlockProbe.Hit.Distance)
		: 0.0f;
	const float BarrelDistanceToMuzzle = BarrelBlockProbe.bHit
		? FMath::Max(BarrelBlockLength - BarrelBlockProbe.Hit.Distance, 0.0f)
		: MAX_flt;
	const float BarrelHardStopAlpha = BarrelBlockProbe.bHit
		? FMath::Max(GetHardStopAlphaFromDistance(BarrelDistanceToMuzzle), BarrelBlockProbe.Alpha * 0.5f)
		: 0.0f;
	WallHardStopTargetAlpha = FMath::Max(MuzzleHardStopAlpha, BarrelHardStopAlpha);
	const float VeryCloseProbeAlpha = FMath::Max(
		CenterProbe.Alpha,
		FMath::Min(FMath::Max(UpperProbe.Alpha, LowerProbe.Alpha), CenterProbe.Alpha + 0.25f)
	);
	const float TopEdgeVeryCloseScale = FMath::Clamp(WeaponValues->WallTopEdgeVeryCloseScale, 0.0f, 1.0f);
	const float VeryCloseMuzzleBlockScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleBlockScale, 0.0f, 1.0f);
	const float VeryCloseMuzzleSourceAlpha = FMath::Lerp(
		WallMuzzleBlockTargetAlpha * VeryCloseMuzzleBlockScale,
		WallMuzzleBlockTargetAlpha * VeryCloseMuzzleBlockScale * TopEdgeVeryCloseScale,
		WallTopEdgeAlpha
	);
	const float VeryCloseSourceAlpha = FMath::Max(
		VeryCloseProbeAlpha,
		FMath::Max(VeryCloseMuzzleSourceAlpha, WallHardStopTargetAlpha)
	);
	const float VeryCloseStartAlpha = FMath::Clamp(WeaponValues->WallVeryCloseStartAlpha, 0.0f, 1.0f);
	const float VeryCloseFullAlpha = FMath::Max(
		FMath::Clamp(WeaponValues->WallVeryCloseFullAlpha, 0.0f, 1.0f),
		VeryCloseStartAlpha + KINDA_SMALL_NUMBER
	);
	WallVeryCloseTargetAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(VeryCloseStartAlpha, VeryCloseFullAlpha),
		FVector2D(0.0f, 1.0f),
		VeryCloseSourceAlpha
	);

	const bool bUpperDominant = UpperProbe.Alpha >= LowerProbe.Alpha;
	const float RawUpAlpha = FMath::Max(UpperProbe.Alpha, bUpperDominant ? CenterProbe.Alpha : 0.0f);
	const float RawDownAlpha = FMath::Max(LowerProbe.Alpha, bUpperDominant ? 0.0f : CenterProbe.Alpha);
	if (WallCeilingAlpha > 0.2f)
	{
		WallUpTargetAlpha = 0.0f;
		WallDownTargetAlpha = FMath::Max(RawDownAlpha, WallCeilingAlpha);
	}
	else if (RawDownAlpha > RawUpAlpha + 0.15f)
	{
		WallDownTargetAlpha = RawDownAlpha;
	}
	else
	{
		WallUpTargetAlpha = RawUpAlpha;
	}
	const float LookDownAlpha = FMath::Clamp((-Forward.Z - 0.12f) / 0.35f, 0.0f, 1.0f);
	const float DownProbePreferenceAlpha = RawDownAlpha > RawUpAlpha + 0.05f ? 1.0f : 0.0f;
	WallMuzzleBlockDownPreferenceTargetAlpha = FMath::Max3(LookDownAlpha, DownProbePreferenceAlpha, WallCeilingAlpha);
	WallMuzzleBlockDownPreferenceAlpha = FMath::FInterpTo(
		WallMuzzleBlockDownPreferenceAlpha,
		WallMuzzleBlockDownPreferenceTargetAlpha,
		DeltaSeconds,
		WeaponValues->WallMuzzleBlockPreferenceBlendSpeed
	);

	const float RawRightAlpha = RightProbe.Alpha;
	const float RawLeftAlpha = LeftProbe.Alpha;
	const float CenterSideSuppressAlpha = CenterProbe.Alpha * 0.65f;
	const float RightSideAlpha = FMath::Clamp(
		RawRightAlpha - FMath::Max(RawLeftAlpha, CenterSideSuppressAlpha),
		0.0f,
		1.0f
	);
	const float LeftSideAlpha = FMath::Clamp(
		RawLeftAlpha - FMath::Max(RawRightAlpha, CenterSideSuppressAlpha),
		0.0f,
		1.0f
	);
	if (FMath::Max(RightSideAlpha, LeftSideAlpha) > 0.0f)
	{
		WallSideTargetAlpha = FMath::Max(RightSideAlpha, LeftSideAlpha);
		WallSideTargetSign = RightSideAlpha >= LeftSideAlpha ? -1.0f : 1.0f;
	}

	WallTargetAlpha = FMath::Max(WallUpTargetAlpha, WallDownTargetAlpha);

	if (WeaponValues->bDrawWallOffsetDebug)
	{
		const auto DrawProbe = [&](const FWallProbeResult& Probe, const FVector& ProbeStart, const FColor& HitColor)
		{
			const FVector ProbeEnd = ProbeStart + Forward * TraceDistance;
			const FColor TraceColor = Probe.bHit ? HitColor : FColor(120, 120, 120);
			DrawDebugLine(
				CachedShooterCharacter->GetWorld(),
				ProbeStart,
				ProbeEnd,
				TraceColor,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				Probe.bHit ? 1.0f : 0.5f
			);
			DrawDebugPoint(
				CachedShooterCharacter->GetWorld(),
				Probe.bHit ? Probe.Hit.ImpactPoint : ProbeEnd,
				DebugPointSize,
				TraceColor,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0
			);
			if (Probe.bHit)
			{
				DrawDebugDirectionalArrow(
					CachedShooterCharacter->GetWorld(),
					Probe.Hit.ImpactPoint,
					Probe.Hit.ImpactPoint + Probe.Hit.Normal * 10.0f,
					4.0f,
					FColor::Green,
					false,
					WeaponValues->WallOffsetDebugDrawTime,
					0,
					0.75f
				);
			}
		};

		DrawProbe(CenterProbe, BaseTraceStart, FColor::Red);
		DrawProbe(UpperProbe, BaseTraceStart + Up * VerticalProbeOffset, FColor::Blue);
		DrawProbe(LowerProbe, BaseTraceStart - Up * VerticalProbeOffset, FColor::Cyan);
		DrawProbe(RightProbe, BaseTraceStart + Right * SideProbeOffset, FColor::Yellow);
		DrawProbe(LeftProbe, BaseTraceStart - Right * SideProbeOffset, FColor(255, 128, 0));

		const FColor MuzzleBlockColor = MuzzleBlockProbe.bHit ? FColor::Magenta : FColor(180, 80, 180);
		DrawDebugLine(
			CachedShooterCharacter->GetWorld(),
			MuzzleBlockTraceStart,
			MuzzleBlockTraceEnd,
			MuzzleBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0,
			MuzzleBlockProbe.bHit ? 1.5f : 0.75f
		);
		DrawDebugPoint(
			CachedShooterCharacter->GetWorld(),
			MuzzleBlockProbe.bHit ? MuzzleBlockProbe.Hit.ImpactPoint : MuzzleBlockTraceEnd,
			DebugPointSize + 2.0f,
			MuzzleBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0
		);
		if (MuzzleBlockProbe.bHit)
		{
			DrawDebugDirectionalArrow(
				CachedShooterCharacter->GetWorld(),
				MuzzleBlockProbe.Hit.ImpactPoint,
				MuzzleBlockProbe.Hit.ImpactPoint + MuzzleBlockProbe.Hit.Normal * 12.0f,
				4.0f,
				FColor::Purple,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				1.0f
			);
		}
		const FColor BarrelBlockColor = BarrelBlockProbe.bHit ? FColor::Orange : FColor(160, 100, 40);
		DrawDebugLine(
			CachedShooterCharacter->GetWorld(),
			BarrelBlockTraceStart,
			BarrelBlockTraceEnd,
			BarrelBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0,
			BarrelBlockProbe.bHit ? 1.25f : 0.6f
		);
		DrawDebugPoint(
			CachedShooterCharacter->GetWorld(),
			BarrelBlockProbe.bHit ? BarrelBlockProbe.Hit.ImpactPoint : BarrelBlockTraceEnd,
			DebugPointSize + 1.0f,
			BarrelBlockColor,
			false,
			WeaponValues->WallOffsetDebugDrawTime,
			0
		);
		if (bHasActualMuzzleSocket)
		{
			DrawDebugPoint(
				CachedShooterCharacter->GetWorld(),
				ActualMuzzleLocation,
				DebugPointSize + 4.0f,
				FColor::White,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0
			);
			DrawDebugDirectionalArrow(
				CachedShooterCharacter->GetWorld(),
				ActualMuzzleLocation,
				ActualMuzzleLocation + ActualMuzzleForward * 18.0f,
				5.0f,
				FColor::Green,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				1.0f
			);
			DrawDebugLine(
				CachedShooterCharacter->GetWorld(),
				MuzzleBlockTraceStart,
				ActualMuzzleLocation,
				FColor::White,
				false,
				WeaponValues->WallOffsetDebugDrawTime,
				0,
				0.5f
			);
		}

		DrawDebugString(
			CachedShooterCharacter->GetWorld(),
			Start + Forward * 45.0f + Up * 18.0f,
			FString::Printf(
				TEXT("Wall Target U %.2f D %.2f C %.2f S %.2f %.0f M %.2f B %.2f HS %.2f VC %.2f TE %.2f P %.2f PB %.2f\nWall Blend U %.2f D %.2f C %.2f S %.2f %.2f M %.2f HS %.2f HSS %.2f VC %.2f TE %.2f"),
				WallUpTargetAlpha,
				WallDownTargetAlpha,
				WallCeilingTargetAlpha,
				WallSideTargetAlpha,
				WallSideTargetSign,
				WallMuzzleBlockTargetAlpha,
				BarrelBlockTargetAlpha,
				WallHardStopTargetAlpha,
				WallVeryCloseTargetAlpha,
				WallTopEdgeTargetAlpha,
				WallMuzzleBlockDownPreferenceAlpha,
				PitchBlockAlpha,
				WallAvoidUpAlpha,
				WallAvoidDownAlpha,
				WallCeilingAlpha,
				WallAvoidSideAlpha,
				WallAvoidSideSign,
				WallMuzzleBlockAlpha,
				WallHardStopAlpha,
				WallHardStopSafetyAlpha,
				WallVeryCloseAlpha,
				WallTopEdgeAlpha
			),
			nullptr,
			WallTargetAlpha > 0.0f ? FColor::Red : FColor(120, 120, 120),
			WeaponValues->WallOffsetDebugDrawTime,
			false,
			1.0f
		);
	}

	const float InterpSpeed = WallTargetAlpha > WallOffsetAlpha
		? WeaponValues->WallBlendInSpeed
		: WeaponValues->WallBlendOutSpeed;

	WallOffsetAlpha = FMath::FInterpTo(
		WallOffsetAlpha,
		WallTargetAlpha,
		DeltaSeconds,
		InterpSpeed
	);

	WallAvoidUpAlpha = FMath::FInterpTo(
		WallAvoidUpAlpha,
		WallUpTargetAlpha,
		DeltaSeconds,
		WallUpTargetAlpha > WallAvoidUpAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	);
	WallAvoidDownAlpha = FMath::FInterpTo(
		WallAvoidDownAlpha,
		WallDownTargetAlpha,
		DeltaSeconds,
		WallDownTargetAlpha > WallAvoidDownAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	);
	WallAvoidSideAlpha = FMath::FInterpTo(
		WallAvoidSideAlpha,
		WallSideTargetAlpha,
		DeltaSeconds,
		WallSideTargetAlpha > WallAvoidSideAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	);
	WallAvoidSideSign = FMath::FInterpTo(
		WallAvoidSideSign,
		WallSideTargetAlpha > KINDA_SMALL_NUMBER ? WallSideTargetSign : 0.0f,
		DeltaSeconds,
		WeaponValues->WallBlendInSpeed
	);
	if (WallMuzzleBlockTargetAlpha > KINDA_SMALL_NUMBER)
	{
		WallMuzzleBlockReleaseHoldTimer = FMath::Max(WeaponValues->WallMuzzleBlockReleaseHoldTime, 0.0f);
	}
	const float EffectiveMuzzleBlockTargetAlpha = WallMuzzleBlockReleaseHoldTimer > 0.0f
		? FMath::Max(WallMuzzleBlockTargetAlpha, WallMuzzleBlockAlpha)
		: WallMuzzleBlockTargetAlpha;
	WallMuzzleBlockAlpha = FMath::FInterpTo(
		WallMuzzleBlockAlpha,
		EffectiveMuzzleBlockTargetAlpha,
		DeltaSeconds,
		EffectiveMuzzleBlockTargetAlpha > WallMuzzleBlockAlpha
			? WeaponValues->WallMuzzleBlockBlendInSpeed
			: WeaponValues->WallMuzzleBlockBlendOutSpeed
	);
	const float HardStopTargetSmoothAlpha = 1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallHardStopTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallHardStopSmoothedTargetAlpha = FMath::Lerp(WallHardStopSmoothedTargetAlpha, WallHardStopTargetAlpha, HardStopTargetSmoothAlpha);
	WallHardStopAlpha = FMath::FInterpTo(
		WallHardStopAlpha,
		WallHardStopSmoothedTargetAlpha,
		DeltaSeconds,
		WallHardStopSmoothedTargetAlpha > WallHardStopAlpha
			? WeaponValues->WallHardStopBlendInSpeed
			: WeaponValues->WallHardStopBlendOutSpeed
	);
	WallHardStopSafetyAlpha = FMath::FInterpTo(
		WallHardStopSafetyAlpha,
		WallHardStopTargetAlpha,
		DeltaSeconds,
		WallHardStopTargetAlpha > WallHardStopSafetyAlpha
			? WeaponValues->WallHardStopSafetyBlendInSpeed
			: WeaponValues->WallHardStopSafetyBlendOutSpeed
	);
	WallVeryCloseAlpha = FMath::FInterpTo(
		WallVeryCloseAlpha,
		WallVeryCloseTargetAlpha,
		DeltaSeconds,
		WallVeryCloseTargetAlpha > WallVeryCloseAlpha
			? WeaponValues->WallVeryCloseBlendInSpeed
			: WeaponValues->WallVeryCloseBlendOutSpeed
	);

	const float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallOffsetAlpha, 2.0f);
	const float EasedMuzzleBlockAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockAlpha, 2.0f);
	const float EasedVeryCloseAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f);
	const float EasedHardStopAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopAlpha, 2.0f);
	const float EasedHardStopSafetyAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopSafetyAlpha, 2.0f);
	const float EasedFinalHardStopAlpha = FMath::Max(EasedHardStopAlpha, EasedHardStopSafetyAlpha);
	const float EasedPoseAlpha = FMath::InterpEaseOut(
		0.0f,
		1.0f,
		FMath::Max(WallAvoidUpAlpha, WallAvoidDownAlpha),
		2.0f
	);
	const float WallStateSuppressAlpha = FMath::Clamp(
		FMath::Max3(EasedPoseAlpha, EasedVeryCloseAlpha, WallCeilingAlpha),
		0.0f,
		1.0f
	);
	const float EasedSideAlpha =
		FMath::InterpEaseInOut(0.0f, 1.0f, WallAvoidSideAlpha, 2.0f) *
		(1.0f - WallStateSuppressAlpha);
	const float SideCloseAlpha =
		EasedSideAlpha *
		FMath::InterpEaseIn(
			0.0f,
			1.0f,
			FMath::Clamp(FMath::Max(RawRightAlpha, RawLeftAlpha), 0.0f, 1.0f),
			2.0f
	);
	const float VeryCloseMuzzleOffsetSuppressScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleOffsetSuppressScale, 0.0f, 1.0f);
	const float MuzzleOffsetSuppressAlpha = FMath::Max(EasedVeryCloseAlpha * VeryCloseMuzzleOffsetSuppressScale, EasedFinalHardStopAlpha);
	const float MuzzleOffsetScale = FMath::Clamp(1.0f - MuzzleOffsetSuppressAlpha, 0.0f, 1.0f);

	ViewModelWallOffsetLoc =
		(WeaponValues->WallMaxOffsetLoc * EasedAlpha) +
		(WeaponValues->WallMaxOffsetLoc * SideCloseAlpha * FMath::Clamp(WeaponValues->WallSideCloseOffsetScale, 0.0f, 1.0f)) +
		(WeaponValues->WallMuzzleBlockLoc * EasedMuzzleBlockAlpha * MuzzleOffsetScale) +
		(WeaponValues->WallVeryCloseLoc * EasedVeryCloseAlpha) +
		(WeaponValues->WallHardStopLoc * EasedFinalHardStopAlpha);
	const float VeryCloseMuzzleRotSuppressScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleRotSuppressScale, 0.0f, 1.0f);
	const float MuzzleRotSuppressAlpha = FMath::Max(EasedVeryCloseAlpha * VeryCloseMuzzleRotSuppressScale, EasedFinalHardStopAlpha);
	const float MuzzleRotScale = FMath::Clamp(1.0f - MuzzleRotSuppressAlpha, 0.0f, 1.0f);
	ViewModelWallOffsetRot =
		(WeaponValues->WallMaxOffsetRot * EasedAlpha) +
		(WeaponValues->WallSideOffsetRot * WallAvoidSideSign * EasedSideAlpha) +
		(WeaponValues->WallMuzzleBlockRot * EasedMuzzleBlockAlpha * (1.0f - WallMuzzleBlockDownPreferenceAlpha) * MuzzleRotScale) +
		(WeaponValues->WallMuzzleBlockDownRot * EasedMuzzleBlockAlpha * WallMuzzleBlockDownPreferenceAlpha * MuzzleRotScale) +
		(WeaponValues->WallVeryCloseRot * EasedVeryCloseAlpha) +
		(WeaponValues->WallHardStopRot * EasedFinalHardStopAlpha);
	WallOffsetLoc = ViewModelWallOffsetLoc;
	WallOffsetRot = ViewModelWallOffsetRot;
}
