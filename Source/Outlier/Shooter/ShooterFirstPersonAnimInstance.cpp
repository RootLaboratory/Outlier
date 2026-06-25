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

	WallOffsetAlpha = FMath::FInterpTo(WallOffsetAlpha, 0.0f, DeltaSeconds, 8.0f);

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
		ViewModelStandLeftUpperArmPitchLoc = FVector::ZeroVector;
		ViewModelStandLeftUpperArmPitchRot = FRotator::ZeroRotator;
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
	ViewModelStandLeftHandJointTargetLoc = StandPitchLeftHandJointTargetLoc;
	ViewModelStandLeftUpperArmPitchLoc = StandPitchLeftUpperArmLoc;
	ViewModelStandLeftUpperArmPitchRot = StandPitchLeftUpperArmRot;

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
	ViewModelWallOffsetLoc = WeaponValues.WallOffsetLoc * WallOffsetAlpha;
	ViewModelWallOffsetRot = WeaponValues.WallOffsetRot * WallOffsetAlpha;

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

				const FVector SocketLocWithOffsets =
					SocketLocalLoc + SprintIKLocRaw;
				const FQuat SocketRotWithOffsets = (
					SocketLocalRot.Quaternion() *
					SprintIKRotRaw.Quaternion()
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
	ViewModelProceduralRuntime.LeftHandIKLoc = ViewModelLeftHandIKLoc;
	ViewModelProceduralRuntime.LeftHandIKRot = ViewModelLeftHandIKRot;
	ViewModelProceduralRuntime.LeftHandGripOffsetLoc = ViewModelLeftHandGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandGripOffsetRot = ViewModelLeftHandGripOffsetRot;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetLoc = WeaponValues.LeftHandReloadGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetRot = WeaponValues.LeftHandReloadGripOffsetRot;
	const float RuntimeLeftHandIKAlpha = bUseFirearmProcedural
		? ViewModelLeftHandIKAlpha * NonReloadCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandReloadIKAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandReloadIKAlpha, 0.0f, 1.0f) * ReloadCrossfadeAlpha
		: 0.0f;
	ViewModelProceduralRuntime.LeftHandIKAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftHandFreeAlpha = bUseFirearmProcedural ? (1.0f - RuntimeLeftHandIKAlpha) : 1.0f;
	const FVector RuntimeLeftHandJointTargetLoc =
		ViewModelLeftHandJointTargetLoc +
		(WeaponValues.LeftHandSprintJointTargetLoc * RuntimeSprintAlpha) +
		RuntimeLeftHandSprintPitchJointTargetLoc +
		RuntimeLeftHandRecoilJointTargetLoc;
	ViewModelProceduralRuntime.LeftHandJointTargetLoc = RuntimeLeftHandJointTargetLoc;
	ViewModelProceduralRuntime.LeftHandReloadIKLoc = WeaponValues.LeftHandReloadIKLoc * ReloadCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadIKRot = WeaponValues.LeftHandReloadIKRot * ReloadCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadJointTargetLoc = ViewModelStandLeftHandJointTargetLoc;
	ViewModelProceduralRuntime.LeftHandReloadIKAlpha = RuntimeLeftHandReloadIKAlpha;
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
	ViewModelProceduralRuntime.LeftUpperArmReloadLoc = ViewModelStandLeftUpperArmPitchLoc;
	ViewModelProceduralRuntime.LeftUpperArmReloadRot = (ViewModelStandLeftUpperArmPitchRot.Quaternion()).Rotator();

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
	ViewModelProceduralRuntime.StartStopLoc = ViewModelStartStopLoc * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.StartStopRot = ViewModelStartStopRot * NonSprintMovementProceduralAlpha;
	ViewModelProceduralRuntime.bIsAiming = bIsAiming;
	ViewModelProceduralRuntime.bIsCrouching = bIsCrouching;
	ViewModelProceduralRuntime.bIsReloading = bIsReloading;
}
