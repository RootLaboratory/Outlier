// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Shooter/ShooterAnimInstance.h"

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
		bIsEquipping = false;
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
	bIsEquipping = CachedShooterCharacter->GetActionLock() == EShooterActionLock::Equip;
	bIsDead = CachedShooterCharacter->IsDead();
	AimPitch = FRotator::NormalizeAxis(CachedShooterCharacter->GetBaseAimRotation().Pitch);

	bIsFalling = CharacterMovement->IsFalling();
	bIsGrounded = CharacterMovement->IsMovingOnGround();
	bIsInAir = bIsFalling;

	// ── 무기 교체 감지와 포즈 알파 초기화 ────────────────────────────────
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
	const float ReloadIKBlendInSpeed = WeaponValues ? WeaponValues->LeftHandReloadIKBlendInSpeed : ReloadBlendInSpeed;
	const float ReloadIKBlendOutSpeed = WeaponValues ? WeaponValues->LeftHandReloadIKBlendOutSpeed : ReloadBlendOutSpeed;
	const float EquipIKBlendInSpeed = WeaponValues ? WeaponValues->LeftHandEquipIKBlendInSpeed : ReloadBlendInSpeed;
	const float EquipIKBlendOutSpeed = WeaponValues ? WeaponValues->LeftHandEquipIKBlendOutSpeed : ReloadBlendOutSpeed;
	const float SlideIKBlendInSpeed = WeaponValues ? WeaponValues->LeftHandSlideIKBlendInSpeed : SlideBlendInSpeed;
	const float SlideIKBlendOutSpeed = WeaponValues ? WeaponValues->LeftHandSlideIKBlendOutSpeed : SlideBlendOutSpeed;
	const float ActionProceduralReleaseTime = WeaponValues ? FMath::Max(WeaponValues->LeftHandActionProceduralReleaseTime, 0.0f) : 0.16f;
	const bool bReloadProceduralActive =
		bIsReloading &&
		bCanUseFirearmProcedural &&
		IsMontageInProceduralActionWindow(CachedShooterCharacter ? CachedShooterCharacter->GetFirstPersonReloadMontage() : nullptr, ActionProceduralReleaseTime);
	const bool bEquipProceduralActive =
		bIsEquipping &&
		bCanUseWeaponPose &&
		IsMontageInProceduralActionWindow(CachedShooterCharacter ? CachedShooterCharacter->GetFirstPersonEquipMontage() : nullptr, ActionProceduralReleaseTime);
	
	// ── 액션 알파 (스프린트/재장전/슬라이드/장착 + 각 IK 블렌드) ─────────
	ViewModelSprintAlpha = FMath::FInterpTo(
		ViewModelSprintAlpha,
		bIsSprinting ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSprinting ? SprintInSpeed : SprintOutSpeed
	);

	ViewModelReloadPoseAlpha = FMath::FInterpTo(
		ViewModelReloadPoseAlpha,
		bReloadProceduralActive ? 1.0f : 0.0f,
		DeltaSeconds,
		bReloadProceduralActive ? ReloadBlendInSpeed : ReloadBlendOutSpeed
	);

	ViewModelReloadIKBlendAlpha = FMath::FInterpTo(
		ViewModelReloadIKBlendAlpha,
		ViewModelReloadPoseAlpha,
		DeltaSeconds,
		ViewModelReloadPoseAlpha > ViewModelReloadIKBlendAlpha ? ReloadIKBlendInSpeed : ReloadIKBlendOutSpeed
	);

	ViewModelSlidePoseAlpha = FMath::FInterpTo(
		ViewModelSlidePoseAlpha,
		(bIsSliding && bCanUseFirearmProcedural) ? 1.0f : 0.0f,
		DeltaSeconds,
		bIsSliding ? SlideBlendInSpeed : SlideBlendOutSpeed
	);

	ViewModelEquipPoseAlpha = FMath::FInterpTo(
		ViewModelEquipPoseAlpha,
		bEquipProceduralActive ? 1.0f : 0.0f,
		DeltaSeconds,
		bEquipProceduralActive ? EquipIKBlendInSpeed : EquipIKBlendOutSpeed
	);
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

	// ── 조준 알파 (벽 근접 시 ADS 해제 반영) ─────────────────────────────
	const FWeaponValues* WeaponValuesForAim = CurrentProceduralValues ? &CurrentProceduralValues->WeaponValues : nullptr;
	const float AimInterpSpeedIn = WeaponValuesForAim ? FMath::Max(WeaponValuesForAim->AimInterpSpeedIn, 0.0f) : 12.0f;
	const float AimInterpSpeedOut = WeaponValuesForAim ? FMath::Max(WeaponValuesForAim->AimInterpSpeedOut, 0.0f) : 12.0f;
	const float WallAimBreakAlpha = ResolveWallAimBreakAlpha(WeaponValuesForAim);
	const bool bWantsAim = bIsAiming;
	const bool bWantsReloadAim = bIsAiming && bIsReloading;
	const float TargetAimAlpha = bWantsAim ? (1.0f - WallAimBreakAlpha) : 0.0f;
	const float TargetReloadAimAlpha = bWantsReloadAim ? (1.0f - WallAimBreakAlpha) : 0.0f;
	const bool bAimBreakingFromWall = WallAimBreakAlpha > KINDA_SMALL_NUMBER;

	ViewModelAimAlpha = FMath::FInterpTo(
		ViewModelAimAlpha,
		TargetAimAlpha,
		DeltaSeconds,
		(bWantsAim && !bAimBreakingFromWall) ? AimInterpSpeedIn : AimInterpSpeedOut
	);

	ReloadAimAlpha = FMath::FInterpTo(
		ReloadAimAlpha,
		TargetReloadAimAlpha,
		DeltaSeconds,
		(bWantsReloadAim && !bAimBreakingFromWall) ? AimInterpSpeedIn : AimInterpSpeedOut
	);

	// ── 이동 관련 알파 (걷기/앉기/스트레이프/낙하) ───────────────────────
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

	// ── 무기 데이터 기반 디테일 (아이들 호흡/린/손가락/가감속 모션) ──────
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

		UpdateFingerMovement(DeltaSeconds, *WeaponValues, bCanPlayIdleDetail);

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
	UpdateFirstPersonProceduralRuntime(DeltaSeconds);

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

bool UShooterFirstPersonAnimInstance::IsMontageInProceduralActionWindow(const UAnimMontage* Montage, float EarlyReleaseTime) const
{
	if (!Montage)
	{
		return true;
	}

	const UAnimInstance* AnimInstance = GetOwningComponent() ? GetOwningComponent()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(Montage))
	{
		return false;
	}

	float WindowEndTime = Montage->GetPlayLength();
	const FName CurrentSectionName = AnimInstance->Montage_GetCurrentSection(Montage);
	if (!CurrentSectionName.IsNone())
	{
		const int32 SectionIndex = Montage->GetSectionIndex(CurrentSectionName);
		if (SectionIndex != INDEX_NONE)
		{
			float SectionStartTime = 0.0f;
			float SectionEndTime = 0.0f;
			Montage->GetSectionStartAndEndTime(SectionIndex, SectionStartTime, SectionEndTime);
			WindowEndTime = SectionEndTime;
		}
	}

	const float CurrentPosition = AnimInstance->Montage_GetPosition(Montage);
	return CurrentPosition < FMath::Max(WindowEndTime - FMath::Max(EarlyReleaseTime, 0.0f), 0.0f);
}

float UShooterFirstPersonAnimInstance::ResolveWallAimBreakAlpha(const FWeaponValues* WeaponValues) const
{
	// 벽 ADS 해제 판정은 3인칭 AnimInstance가 단일 기준으로 계산한다 — 두 시점이
	// 같은 프레임에 같은 값으로 조준을 해제하기 위함
	if (CachedShooterCharacter)
	{
		if (const USkeletalMeshComponent* BodyMesh = CachedShooterCharacter->GetMesh())
		{
			if (const UShooterAnimInstance* BodyAnimInstance = Cast<UShooterAnimInstance>(BodyMesh->GetAnimInstance()))
			{
				return BodyAnimInstance->GetWallAimBreakAlpha();
			}
		}
	}

	// 몸 메시에 Shooter 인스턴스가 없으면 자체 벽 알파로 로컬 계산 (폴백)
	const float WallAimBreakStartAlpha = WeaponValues
		? FMath::Clamp(WeaponValues->WallAimBreakStartAlpha, 0.0f, 1.0f)
		: 0.55f;
	const float WallAimBreakFullAlpha = WeaponValues
		? FMath::Max(FMath::Clamp(WeaponValues->WallAimBreakFullAlpha, 0.0f, 1.0f), WallAimBreakStartAlpha + KINDA_SMALL_NUMBER)
		: 0.85f;
	const float WallAimBlockSourceAlpha = FMath::Clamp(
		FMath::Max3(WallVeryCloseAlpha, WallHardStopAlpha, WallMuzzleBlockAlpha),
		0.0f,
		1.0f
	);
	return FMath::GetMappedRangeValueClamped(
		FVector2D(WallAimBreakStartAlpha, WallAimBreakFullAlpha),
		FVector2D(0.0f, 1.0f),
		WallAimBlockSourceAlpha
	);
}

void UShooterFirstPersonAnimInstance::UpdateFingerMovement(float DeltaSeconds, const FWeaponValues& WeaponValues, bool bCanPlayIdleDetail)
{
	const float FingerBaseAlpha = FMath::Clamp(WeaponValues.FingerMovementAlpha, 0.0f, 1.0f);
	if (!bCanPlayIdleDetail || FingerBaseAlpha <= KINDA_SMALL_NUMBER)
	{
		bFingerMovementPulseActive = false;
		FingerMovementPulseTime = 0.0f;
		FingerMovementCooldownTime = 0.0f;
		FingerMovementAlpha = 0.0f;
		return;
	}

	if (bFingerMovementPulseActive)
	{
		// 펄스 진행 중: 시간이 다 되면 멈추고 다음 발동까지의 쿨다운을 추첨
		FingerMovementPulseTime = FMath::Max(FingerMovementPulseTime - DeltaSeconds, 0.0f);
		FingerMovementAlpha = FingerBaseAlpha;

		if (FingerMovementPulseTime <= 0.0f)
		{
			bFingerMovementPulseActive = false;
			FingerMovementAlpha = 0.0f;
			FingerMovementCooldownTime = FMath::RandRange(
				FMath::Min(WeaponValues.FingerMovementIntervalMinTime, WeaponValues.FingerMovementIntervalMaxTime),
				FMath::Max(WeaponValues.FingerMovementIntervalMinTime, WeaponValues.FingerMovementIntervalMaxTime)
			);
		}
	}
	else
	{
		// 쿨다운 소진 시 새 펄스 시작 (지속 시간 추첨)
		FingerMovementCooldownTime -= DeltaSeconds;

		if (FingerMovementCooldownTime <= 0.0f)
		{
			bFingerMovementPulseActive = true;
			FingerMovementPulseTime = FMath::RandRange(
				FMath::Min(WeaponValues.FingerMovementPulseMinTime, WeaponValues.FingerMovementPulseMaxTime),
				FMath::Max(WeaponValues.FingerMovementPulseMinTime, WeaponValues.FingerMovementPulseMaxTime)
			);
			FingerMovementAlpha = FingerBaseAlpha;
		}
		else
		{
			FingerMovementAlpha = 0.0f;
		}
	}
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
		ViewModelLeftHandActionReturnGripOffsetLoc = FVector::ZeroVector;
		ViewModelLeftHandActionReturnGripOffsetRot = FRotator::ZeroRotator;
		LastLeftHandActionGripOffsetLoc = FVector::ZeroVector;
		LastLeftHandActionGripOffsetRot = FRotator::ZeroRotator;
		PreviousLeftHandActionIKAlpha = 0.0f;
		LeftHandActionReturnTimer = 0.0f;
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

	// ── 무기 데이터 → 뷰모델 포즈/피치 커브 값 갱신 ──────────────────────
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

	// ── 왼손 그립 소켓 재구성 (무기 소켓 → ik_hand_gun 로컬 오프셋) ──────
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
				const float ActionPoseAlpha = FMath::Clamp(FMath::Max(ViewModelReloadPoseAlpha, ViewModelEquipPoseAlpha), 0.0f, 1.0f);
				const float WallActionLeftHandScale = FMath::Lerp(
					1.0f,
					FMath::Clamp(WeaponValues.WallActionLeftHandScale, 0.0f, 1.0f),
					ActionPoseAlpha
				);
				const float WallVeryCloseSocketOffsetAlpha = FMath::Clamp(
					FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f) *
					WallActionLeftHandScale *
					WeaponValues.LeftHandWallVeryCloseSocketOffsetAlphaScale,
					0.0f,
					1.0f
				);
				const float WallMuzzleBlockSocketOffsetAlpha = FMath::Clamp(
					FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockAlpha, 2.0f) *
					WallActionLeftHandScale *
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
	const float ActionIKBlendAlphaForTarget = FMath::Clamp(FMath::Max(ViewModelReloadIKBlendAlpha, ViewModelEquipIKBlendAlpha), 0.0f, 1.0f);
	const bool bReturningFromActionIK =
		(ActionIKBlendAlphaForTarget > KINDA_SMALL_NUMBER || LeftHandActionReturnTimer > 0.0f);
	if (PreviousLeftHandActionIKAlpha > KINDA_SMALL_NUMBER && ActionIKBlendAlphaForTarget <= KINDA_SMALL_NUMBER)
	{
		LeftHandActionReturnTimer = 0.12f;
	}
	else
	{
		LeftHandActionReturnTimer = FMath::Max(LeftHandActionReturnTimer - DeltaSeconds, 0.0f);
	}
	const float LeftHandIKTargetBlendSpeed = bReturningFromActionIK
		? FMath::Max(WeaponValues.LeftHandIKTargetActionReturnBlendSpeed, 0.0f)
		: FMath::Max(WeaponValues.LeftHandIKTargetBlendSpeed, 0.0f);
	// 벽 단계는 각자의 알파로 이미 그립 타깃을 스무딩하고 있다. 여기서 또
	// 보간하면 벽 오프셋 복귀 중 왼손이 소켓을 늦게 따라가므로, 벽 단계가
	// 하나라도 활성일 때는 스냅한다
	const float WallGripStageAlpha = FMath::Max3(
		FMath::Max(WallOffsetAlpha, WallMuzzleBlockAlpha),
		WallVeryCloseAlpha,
		FMath::Max(WallHardStopAlpha, WallHardStopSafetyAlpha)
	);
	const bool bWallGripSnap = WallGripStageAlpha > KINDA_SMALL_NUMBER && !bReturningFromActionIK;
	if (!bLeftHandIKSocketValid || LeftHandIKTargetBlendSpeed <= 0.0f || bWallGripSnap)
	{
		ViewModelLeftHandGripOffsetLoc = TargetLeftHandGripOffsetLoc;
		ViewModelLeftHandGripOffsetRot = TargetLeftHandGripOffsetRot;
	}
	else
	{
		ViewModelLeftHandGripOffsetLoc = FMath::VInterpTo(
			ViewModelLeftHandGripOffsetLoc,
			TargetLeftHandGripOffsetLoc,
			DeltaSeconds,
			LeftHandIKTargetBlendSpeed
		);
		ViewModelLeftHandGripOffsetRot = FMath::RInterpTo(
			ViewModelLeftHandGripOffsetRot,
			TargetLeftHandGripOffsetRot,
			DeltaSeconds,
			LeftHandIKTargetBlendSpeed
		);
	}

	ViewModelMovementLoc = ViewModelSprintPoseLoc;
	ViewModelMovementRot = ViewModelSprintPoseRot;
}

// 프레임마다 계산해 둔 개별 알파/오프셋들을 ABP가 소비하는 런타임 구조체
// (ViewModelProceduralRuntime) 하나로 합성한다
void UShooterFirstPersonAnimInstance::UpdateFirstPersonProceduralRuntime(float DeltaSeconds)
{
	if (!CurrentProceduralValues)
	{
		ViewModelProceduralRuntime = FFirstPersonProceduralAnimRuntime();
		ViewModelLeftHandActionReturnGripOffsetLoc = FVector::ZeroVector;
		ViewModelLeftHandActionReturnGripOffsetRot = FRotator::ZeroRotator;
		LastLeftHandActionGripOffsetLoc = FVector::ZeroVector;
		LastLeftHandActionGripOffsetRot = FRotator::ZeroRotator;
		PreviousLeftHandActionIKAlpha = 0.0f;
		LeftHandActionReturnTimer = 0.0f;
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
	const float EquipPoseAlpha = FMath::Clamp(ViewModelEquipPoseAlpha, 0.0f, 1.0f);
	const float EquipCrossfadeAlpha = EquipPoseAlpha;
	const float ReloadIKCrossfadeAlpha = FMath::Clamp(ViewModelReloadIKBlendAlpha, 0.0f, 1.0f);
	const float EquipIKCrossfadeAlpha = FMath::Clamp(ViewModelEquipIKBlendAlpha, 0.0f, 1.0f);
	const float NonActionCrossfadeAlpha = 1.0f - FMath::Clamp(FMath::Max(ReloadIKCrossfadeAlpha, EquipIKCrossfadeAlpha), 0.0f, 1.0f);
	const float WallActionAlpha = FMath::Clamp(FMath::Max(ReloadCrossfadeAlpha, EquipCrossfadeAlpha), 0.0f, 1.0f);
	const float WallActionPoseScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.WallActionPoseScale, 0.0f, 1.0f),
		WallActionAlpha
	);
	const float WallActionLeftHandScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.WallActionLeftHandScale, 0.0f, 1.0f),
		WallActionAlpha
	);
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
	const float RuntimeAimWalkAnimScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues.AimWalkAnimScale, 0.0f, 1.0f),
		FMath::Clamp(ViewModelAimAlpha, 0.0f, 1.0f)
	);
	const float NonSprintMovementProceduralAlpha = FirearmMinorProceduralAlpha * RuntimeAimMovementProceduralScale;
	const float NonSprintWalkAnimProceduralAlpha = FirearmMinorProceduralAlpha * RuntimeAimWalkAnimScale;

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
	const float ForwardWalkAnimProceduralAlpha = bIsSprinting ? RuntimeSprintAlpha : NonSprintWalkAnimProceduralAlpha;
	const FVector RuntimeForwardWalkLoc = ViewModelForwardWalkLoc * ForwardWalkProceduralAlpha;
	const FRotator RuntimeForwardWalkRot = ViewModelForwardWalkRot * ViewModelWalkAnimAlpha * ForwardWalkProceduralAlpha;
	const float RuntimeForwardWalkAlpha = ViewModelWalkAnimAlpha * ForwardWalkAnimProceduralAlpha;
	const FVector RuntimeJumpLandLoc = ViewModelJumpLandLoc * FirearmMinorProceduralAlpha;
	const FRotator RuntimeJumpLandRot = ViewModelJumpLandRot * FirearmMinorProceduralAlpha;
	const float RuntimeJumpLandAlpha = ViewModelJumpLandAlpha * FirearmMinorProceduralAlpha;

	const FVector RuntimeWeaponRootLocOffset =
		ViewModelHipPoseLoc +
		(ViewModelMovementLoc * RuntimeSprintAlpha) +
		RuntimeRecoilLoc +
		RuntimeSwayLoc +
		(ViewModelForwardWalkLoc * ViewModelWalkAnimAlpha * ForwardWalkProceduralAlpha) +
		(ViewModelForwardWalkAnimLoc * ForwardWalkAnimProceduralAlpha) +
		(ViewModelStartStopLoc * NonSprintMovementProceduralAlpha) +
		RuntimeJumpLandLoc;
	const FRotator RuntimeWeaponRootRotOffset = (
		ViewModelHipPoseRot.Quaternion() *
		(ViewModelMovementRot * RuntimeSprintAlpha).Quaternion() *
		RuntimeRecoilRot.Quaternion() *
		RuntimeSwayRot.Quaternion() *
		RuntimeForwardWalkRot.Quaternion() *
		(ViewModelForwardWalkAnimRot * ForwardWalkAnimProceduralAlpha).Quaternion() *
		(ViewModelCurrentStrafeWalkRot * NonSprintMovementProceduralAlpha).Quaternion() *
		(ViewModelStartStopRot * NonSprintMovementProceduralAlpha).Quaternion() *
		RuntimeJumpLandRot.Quaternion() 
		).Rotator();

	ViewModelProceduralRuntime.WeaponPoseAlpha = WeaponPoseAlpha;
	ViewModelProceduralRuntime.WeaponRootLocOffset = RuntimeWeaponRootLocOffset;
	ViewModelProceduralRuntime.WeaponRootRotOffset = RuntimeWeaponRootRotOffset;

	ViewModelProceduralRuntime.RightHandIKLocOffset =
		(WeaponValues.RightHandReloadIKLocOffset * ReloadCrossfadeAlpha) +
		(WeaponValues.RightHandEquipIKLocOffset * EquipCrossfadeAlpha);
	ViewModelProceduralRuntime.RightHandIKRotOffset =
		(WeaponValues.RightHandReloadIKRotOffset * ReloadCrossfadeAlpha) +
		(WeaponValues.RightHandEquipIKRotOffset * EquipCrossfadeAlpha);
	ViewModelProceduralRuntime.SprintPoseLoc = ViewModelSprintPoseLoc;
	ViewModelProceduralRuntime.SprintPoseRot = ViewModelSprintPoseRot;
	ViewModelProceduralRuntime.SprintAlpha = RuntimeSprintAlpha;
	// ── 벽 단계 알파의 런타임 합성 ───────────────────────────────────────
	// 근접 단계(VeryClose/HardStop)가 강해지면 회피 포즈와 MuzzleBlock 팔
	// 보정을 억제해서, 서로 다른 벽 대응이 팔 위에 중첩되지 않게 한다
	const float ScaledWallAvoidUpAlpha = WallAvoidUpAlpha * WallActionPoseScale;
	const float ScaledWallAvoidDownAlpha = WallAvoidDownAlpha * WallActionPoseScale;
	const float ScaledWallMuzzleBlockAlpha = WallMuzzleBlockAlpha * WallActionPoseScale;
	const float ScaledWallVeryCloseAlpha = WallVeryCloseAlpha * WallActionPoseScale;
	const float ScaledWallCeilingAlpha = WallCeilingAlpha * WallActionPoseScale;
	const float WallPoseBlendAlpha = FMath::Clamp(FMath::Max3(ScaledWallAvoidUpAlpha, ScaledWallAvoidDownAlpha, ScaledWallMuzzleBlockAlpha), 0.0f, 1.0f);
	const float WallSideSuppressAlpha = FMath::Clamp(FMath::Max3(WallPoseBlendAlpha, ScaledWallVeryCloseAlpha, ScaledWallCeilingAlpha), 0.0f, 1.0f);
	const float WallSideOnlyAlpha = FMath::Clamp(WallAvoidSideAlpha * WallActionLeftHandScale * (1.0f - WallSideSuppressAlpha), 0.0f, 1.0f);
	const float WallVeryClosePoseSuppressAlpha = FMath::Clamp(
		FMath::Max(
			FMath::InterpEaseOut(0.0f, 1.0f, ScaledWallVeryCloseAlpha, 2.0f) *
			WeaponValues.WallVeryClosePoseSuppressScale,
			FMath::Max(WallHardStopAlpha, WallHardStopSafetyAlpha)
		),
		0.0f,
		1.0f
	);
	const float WallMuzzleBlockOnlyAlpha = FMath::Clamp(ScaledWallMuzzleBlockAlpha * (1.0f - WallVeryClosePoseSuppressAlpha), 0.0f, 1.0f);
	const float WallMuzzleBlockArmAlpha = FMath::Clamp(
		FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockOnlyAlpha, 2.0f) *
		WallActionLeftHandScale *
		WeaponValues.LeftHandWallMuzzleBlockArmAlphaScale,
		0.0f,
		1.0f
	);
	const float WallActionHandFollowScale = FMath::Clamp(WeaponValues.WallActionHandFollowScale, 0.0f, 1.0f);
	const float WallActionArmFollowScale = FMath::Clamp(WeaponValues.WallActionArmFollowScale, 0.0f, 1.0f);
	const FVector WallActionHandFollowLoc = ViewModelWallOffsetLoc * WallActionHandFollowScale;
	const FRotator WallActionHandFollowRot = ViewModelWallOffsetRot * WallActionHandFollowScale;
	const FVector WallActionArmFollowLoc = ViewModelWallOffsetLoc * WallActionArmFollowScale;
	const FRotator WallActionArmFollowRot = ViewModelWallOffsetRot * WallActionArmFollowScale;
	const float WallCloseFollowAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(0.05f, 0.35f),
		FVector2D(0.0f, 1.0f),
		FMath::Clamp(FMath::Max3(ScaledWallVeryCloseAlpha, WallHardStopAlpha, WallHardStopSafetyAlpha), 0.0f, 1.0f)
	);
	const float WallCloseArmFollowScale = FMath::Clamp(WeaponValues.WallCloseArmFollowScale, 0.0f, 1.0f) * WallCloseFollowAlpha;
	const FVector WallCloseArmFollowLoc = ViewModelWallOffsetLoc * WallCloseArmFollowScale;
	const FRotator WallCloseArmFollowRot = ViewModelWallOffsetRot * WallCloseArmFollowScale;
	// ── 액션(재장전/장착) 중 왼손 그립과 복귀 오프셋 ─────────────────────
	const float LeftHandActionIKAlpha = FMath::Clamp(FMath::Max(ReloadIKCrossfadeAlpha, EquipIKCrossfadeAlpha), 0.0f, 1.0f);
	const FVector ReloadActionGripOffsetLoc = WeaponValues.LeftHandReloadGripOffsetLoc + WallActionHandFollowLoc;
	const FRotator ReloadActionGripOffsetRot = WeaponValues.LeftHandReloadGripOffsetRot + WallActionHandFollowRot;
	const FVector EquipActionGripOffsetLoc = WeaponValues.LeftHandEquipGripOffsetLoc + WallActionHandFollowLoc;
	const FRotator EquipActionGripOffsetRot = WeaponValues.LeftHandEquipGripOffsetRot + WallActionHandFollowRot;
	const float ActionGripWeight = ReloadIKCrossfadeAlpha + EquipIKCrossfadeAlpha;
	if (ActionGripWeight > KINDA_SMALL_NUMBER)
	{
		LastLeftHandActionGripOffsetLoc =
			((ReloadActionGripOffsetLoc * ReloadIKCrossfadeAlpha) + (EquipActionGripOffsetLoc * EquipIKCrossfadeAlpha)) /
			ActionGripWeight;
		LastLeftHandActionGripOffsetRot =
			((ReloadActionGripOffsetRot * ReloadIKCrossfadeAlpha) + (EquipActionGripOffsetRot * EquipIKCrossfadeAlpha)) *
			(1.0f / ActionGripWeight);
	}
	if (PreviousLeftHandActionIKAlpha > KINDA_SMALL_NUMBER && LeftHandActionIKAlpha <= KINDA_SMALL_NUMBER)
	{
		ViewModelLeftHandActionReturnGripOffsetLoc =
			(LastLeftHandActionGripOffsetLoc - ViewModelLeftHandGripOffsetLoc).GetClampedToMaxSize(
				FMath::Max(WeaponValues.LeftHandActionReturnMaxLocOffset, 0.0f)
			);
		ViewModelLeftHandActionReturnGripOffsetRot = (LastLeftHandActionGripOffsetRot - ViewModelLeftHandGripOffsetRot).GetNormalized();
		const float MaxReturnRotOffset = FMath::Max(WeaponValues.LeftHandActionReturnMaxRotOffset, 0.0f);
		ViewModelLeftHandActionReturnGripOffsetRot.Pitch = FMath::Clamp(
			ViewModelLeftHandActionReturnGripOffsetRot.Pitch,
			-MaxReturnRotOffset,
			MaxReturnRotOffset
		);
		ViewModelLeftHandActionReturnGripOffsetRot.Yaw = FMath::Clamp(
			ViewModelLeftHandActionReturnGripOffsetRot.Yaw,
			-MaxReturnRotOffset,
			MaxReturnRotOffset
		);
		ViewModelLeftHandActionReturnGripOffsetRot.Roll = FMath::Clamp(
			ViewModelLeftHandActionReturnGripOffsetRot.Roll,
			-MaxReturnRotOffset,
			MaxReturnRotOffset
		);
	}
	else if (LeftHandActionIKAlpha > KINDA_SMALL_NUMBER)
	{
		ViewModelLeftHandActionReturnGripOffsetLoc = FVector::ZeroVector;
		ViewModelLeftHandActionReturnGripOffsetRot = FRotator::ZeroRotator;
	}
	else
	{
		const float ReturnBlendSpeed = FMath::Max(WeaponValues.LeftHandActionReturnBlendSpeed, 0.0f);
		if (ReturnBlendSpeed <= 0.0f)
		{
			ViewModelLeftHandActionReturnGripOffsetLoc = FVector::ZeroVector;
			ViewModelLeftHandActionReturnGripOffsetRot = FRotator::ZeroRotator;
		}
		else
		{
			ViewModelLeftHandActionReturnGripOffsetLoc = FMath::VInterpTo(
				ViewModelLeftHandActionReturnGripOffsetLoc,
				FVector::ZeroVector,
				DeltaSeconds,
				ReturnBlendSpeed
			);
			ViewModelLeftHandActionReturnGripOffsetRot = FMath::RInterpTo(
				ViewModelLeftHandActionReturnGripOffsetRot,
				FRotator::ZeroRotator,
				DeltaSeconds,
				ReturnBlendSpeed
			);
		}
	}
	PreviousLeftHandActionIKAlpha = LeftHandActionIKAlpha;
	ViewModelProceduralRuntime.LeftHandIKLoc = ViewModelLeftHandIKLoc;
	ViewModelProceduralRuntime.LeftHandIKRot = ViewModelLeftHandIKRot;
	// 그립 오프셋에 벽 당김(WallOffsetLoc)을 다시 더하지 않는다: 그래프에서
	// ik_hand_l은 벽 오프셋이 이미 적용된 ik_hand_gun에서 복사되므로 손은
	// 무기를 자동으로 따라간다. 여기서 또 더하면 이중 적용이 되어 근접
	// 단계(VeryClose/HardStop)에서 손이 그립을 벗어나 떠 보인다
	ViewModelProceduralRuntime.LeftHandGripOffsetLoc =
		ViewModelLeftHandGripOffsetLoc +
		(WeaponValues.LeftHandWallSideIKLocOffset * WallSideOnlyAlpha * WallAvoidSideSign) +
		ViewModelLeftHandActionReturnGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandGripOffsetRot =
		ViewModelLeftHandGripOffsetRot +
		ViewModelLeftHandActionReturnGripOffsetRot;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetLoc =
		ReloadActionGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandReloadGripOffsetRot =
		ReloadActionGripOffsetRot;
	ViewModelProceduralRuntime.LeftHandEquipGripOffsetLoc =
		EquipActionGripOffsetLoc;
	ViewModelProceduralRuntime.LeftHandEquipGripOffsetRot =
		EquipActionGripOffsetRot;
	const float RuntimeLeftHandIKAlpha = bUseFirearmProcedural
		? ViewModelLeftHandIKAlpha * NonActionCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandReloadIKAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandReloadIKAlpha, 0.0f, 1.0f) * ReloadIKCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandReloadArmAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandReloadArmAlpha, 0.0f, 1.0f) * ReloadCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandEquipIKAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandEquipIKAlpha, 0.0f, 1.0f) * EquipIKCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandEquipArmAlpha = bUseFirearmProcedural
		? FMath::Clamp(WeaponValues.LeftHandEquipArmAlpha, 0.0f, 1.0f) * EquipCrossfadeAlpha
		: 0.0f;
	const float RuntimeLeftHandAnyIKAlpha = FMath::Clamp(
		RuntimeLeftHandIKAlpha + RuntimeLeftHandReloadIKAlpha + RuntimeLeftHandEquipIKAlpha,
		0.0f,
		1.0f
	);
	ViewModelProceduralRuntime.LeftHandIKAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftHandFreeAlpha = bUseFirearmProcedural ? (1.0f - RuntimeLeftHandAnyIKAlpha) : 1.0f;
	const FVector RuntimeLeftHandJointTargetLoc =
		ViewModelLeftHandJointTargetLoc +
		(WeaponValues.LeftHandSprintJointTargetLoc * RuntimeSprintAlpha) +
		RuntimeLeftHandSprintPitchJointTargetLoc +
		RuntimeLeftHandRecoilJointTargetLoc +
		(WeaponValues.LeftHandWallMuzzleBlockJointTargetLoc * WallMuzzleBlockArmAlpha) +
		WallCloseArmFollowLoc;
	ViewModelProceduralRuntime.LeftHandJointTargetLoc = RuntimeLeftHandJointTargetLoc;
	ViewModelProceduralRuntime.LeftHandReloadIKLoc =
		(WeaponValues.LeftHandReloadIKLoc + WallActionHandFollowLoc) * ReloadIKCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadIKRot =
		(WeaponValues.LeftHandReloadIKRot + WallActionHandFollowRot) * ReloadIKCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandReloadJointTargetLoc =
		ViewModelStandLeftHandJointTargetLoc +
		WallActionArmFollowLoc +
		(WeaponValues.LeftHandReloadJointTargetLoc * RuntimeLeftHandReloadArmAlpha);
	ViewModelProceduralRuntime.LeftHandReloadIKAlpha = RuntimeLeftHandReloadIKAlpha;
	ViewModelProceduralRuntime.LeftHandEquipIKLoc =
		(WeaponValues.LeftHandEquipIKLoc + WallActionHandFollowLoc) * EquipIKCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandEquipIKRot =
		(WeaponValues.LeftHandEquipIKRot + WallActionHandFollowRot) * EquipIKCrossfadeAlpha;
	ViewModelProceduralRuntime.LeftHandEquipJointTargetLoc =
		ViewModelStandLeftHandJointTargetLoc +
		WallActionArmFollowLoc +
		(WeaponValues.LeftHandEquipJointTargetLoc * RuntimeLeftHandEquipArmAlpha);
	ViewModelProceduralRuntime.LeftHandEquipIKAlpha = RuntimeLeftHandEquipIKAlpha;
	ViewModelProceduralRuntime.LeftUpperArmPitchLoc =
		ViewModelLeftUpperArmPitchLoc +
		(WeaponValues.LeftUpperArmSprintLoc * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchLoc +
		RuntimeLeftUpperArmRecoilLoc +
		(WeaponValues.LeftUpperArmWallMuzzleBlockLoc * WallMuzzleBlockArmAlpha) +
		WallCloseArmFollowLoc;
	ViewModelProceduralRuntime.LeftUpperArmPitchRot =
		ViewModelLeftUpperArmPitchRot +
		(WeaponValues.LeftUpperArmSprintRot * RuntimeSprintAlpha) +
		RuntimeLeftUpperArmSprintPitchRot +
		RuntimeLeftUpperArmRecoilRot +
		(WeaponValues.LeftUpperArmWallMuzzleBlockRot * WallMuzzleBlockArmAlpha) +
		WallCloseArmFollowRot;
	ViewModelProceduralRuntime.LeftUpperArmPitchAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftLowerArmPitchRot =
		ViewModelLeftLowerArmPitchRot +
		(WeaponValues.LeftLowerArmWallMuzzleBlockRot * WallMuzzleBlockArmAlpha);
	ViewModelProceduralRuntime.LeftLowerArmPitchAlpha = RuntimeLeftHandIKAlpha;
	ViewModelProceduralRuntime.LeftUpperArmReloadLoc =
		ViewModelStandLeftUpperArmPitchLoc +
		WallActionArmFollowLoc +
		(WeaponValues.LeftUpperArmReloadLoc * RuntimeLeftHandReloadArmAlpha);
	ViewModelProceduralRuntime.LeftUpperArmReloadRot = BlendRotatorOffset(
		ViewModelStandLeftUpperArmPitchRot + WallActionArmFollowRot,
		ViewModelStandLeftUpperArmPitchRot + WallActionArmFollowRot + WeaponValues.LeftUpperArmReloadRot,
		RuntimeLeftHandReloadArmAlpha
	);
	ViewModelProceduralRuntime.LeftLowerArmReloadRot = BlendRotatorOffset(
		ViewModelStandLeftLowerArmPitchRot,
		ViewModelStandLeftLowerArmPitchRot + WeaponValues.LeftLowerArmReloadRot,
		RuntimeLeftHandReloadArmAlpha
	);
	ViewModelProceduralRuntime.LeftUpperArmEquipLoc =
		ViewModelStandLeftUpperArmPitchLoc +
		WallActionArmFollowLoc +
		(WeaponValues.LeftUpperArmEquipLoc * RuntimeLeftHandEquipArmAlpha);
	ViewModelProceduralRuntime.LeftUpperArmEquipRot = BlendRotatorOffset(
		ViewModelStandLeftUpperArmPitchRot + WallActionArmFollowRot,
		ViewModelStandLeftUpperArmPitchRot + WallActionArmFollowRot + WeaponValues.LeftUpperArmEquipRot,
		RuntimeLeftHandEquipArmAlpha
	);
	ViewModelProceduralRuntime.LeftLowerArmEquipRot = BlendRotatorOffset(
		ViewModelStandLeftLowerArmPitchRot,
		ViewModelStandLeftLowerArmPitchRot + WeaponValues.LeftLowerArmEquipRot,
		RuntimeLeftHandEquipArmAlpha
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
	ViewModelProceduralRuntime.ForwardWalkAnimLoc = ViewModelForwardWalkAnimLoc * ForwardWalkAnimProceduralAlpha;
	ViewModelProceduralRuntime.ForwardWalkAnimRot = ViewModelForwardWalkAnimRot * ForwardWalkAnimProceduralAlpha;
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
		FMath::Max(ScaledWallAvoidUpAlpha, MuzzleBlockUpPoseAlpha) *
		(1.0f - FMath::Clamp(ScaledWallCeilingAlpha, 0.0f, 1.0f)) *
		(1.0f - WallVeryClosePoseSuppressAlpha) *
		WeaponValues.WallAvoidUpPoseAlphaScale,
		0.0f,
		1.0f
	);
	ViewModelProceduralRuntime.WallAvoidUpAlpha = RuntimeWallAvoidUpAlpha;
	ViewModelProceduralRuntime.WallAvoidDownAlpha = FMath::Clamp(
		FMath::Max3(
			ScaledWallAvoidDownAlpha * (1.0f - WallVeryClosePoseSuppressAlpha),
			MuzzleBlockDownPoseAlpha,
			ScaledWallCeilingAlpha * (1.0f - WallVeryClosePoseSuppressAlpha)
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

void UShooterFirstPersonAnimInstance::ResetWallOffsetState(bool bResetPoseAssets)
{
	WallOffsetAlpha = 0.0f;
	WallSmoothedTargetAlpha = 0.0f;
	WallAvoidUpAlpha = 0.0f;
	WallUpSmoothedTargetAlpha = 0.0f;
	WallAvoidDownAlpha = 0.0f;
	WallDownSmoothedTargetAlpha = 0.0f;
	WallAvoidSideAlpha = 0.0f;
	WallSideSmoothedTargetAlpha = 0.0f;
	WallAvoidSideSign = 0.0f;
	WallMuzzleBlockAlpha = 0.0f;
	WallMuzzleBlockSmoothedTargetAlpha = 0.0f;
	WallVeryCloseAlpha = 0.0f;
	WallVeryCloseSmoothedTargetAlpha = 0.0f;
	WallVeryCloseTargetAlpha = 0.0f;
	WallVeryCloseOwnTargetAlpha = 0.0f;
	WallHardStopOwnTargetAlpha = 0.0f;
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
	if (bResetPoseAssets)
	{
		WallAvoidUpPose = nullptr;
		WallAvoidDownPose = nullptr;
	}
	WallOffsetLoc = FVector::ZeroVector;
	WallOffsetRot = FRotator::ZeroRotator;
	ViewModelWallOffsetLoc = FVector::ZeroVector;
	ViewModelWallOffsetRot = FRotator::ZeroRotator;
}

// 벽 근접 대응 파이프라인. 카메라 기준 프로브로 벽을 측정해서
// 단계별 알파(WallOffset → MuzzleBlock/Barrel → VeryClose → HardStop)와
// 회피 포즈 알파를 만들고, 최종 뷰모델 위치/회전 오프셋을 합성한다.
// 근접 단계(VeryClose/HardStop)는 3인칭과 raw 타깃을 union해 상태를 통일한다
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
		ResetWallOffsetState(/*bResetPoseAssets=*/true);
		return;
	}

	UCameraComponent* Camera = CachedShooterCharacter->GetFirstPersonCameraComponent();
	if (!Camera)
	{
		ResetWallOffsetState(/*bResetPoseAssets=*/false);
		return;
	}

	WallAvoidUpPose = WeaponValues->WallAvoidUpPose;
	WallAvoidDownPose = WeaponValues->WallAvoidDownPose;

	// ── 1. 카메라 기준 트레이스 파라미터 ─────────────────────────────────
	// 가파른 피치에서는 총열이 벽에 나란히 눕기 때문에 반경/길이를 부스트한다
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

	// ── 2. 십자 프로브 스윕 (중앙/상/하/좌/우) ────────────────────────────
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

	// ── 3. 경계 판정 ─────────────────────────────────────────────────────
	// TopEdge: 아래 프로브만 벽을 봄(벽 위 모서리 너머를 조준)
	// Ceiling: 위 프로브만 벽을 봄(경계 아래에서 위쪽이 막힘)
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

	// ── 4. 총구/총열 차단 프로브 ─────────────────────────────────────────
	// 총열(Barrel)은 총구 기준점 뒤쪽 구간을 스윕해서, 조준 레이가 벽을
	// 비껴가도 벽에 나란히 누운 총열이 감지되게 한다
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

	// ── 5. HardStop: 실제 히트 거리 기반 최근접 ──────────────────────────
	// 선형 프로브 알파는 벽이 아직 멀 때 이미 포화되므로, 최근접 정보는
	// 히트 거리로 직접 램프한다
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

	// ── 6. VeryClose + 3인칭으로 raw 타깃 publish ────────────────────────
	// 근접 단계 raw 타깃은 3인칭이 읽어가 자기 WallTight 타깃과 union한다
	// (FP → TP 단방향). 역방향으로 3인칭 값을 수입하면 "몸은 벽에 붙었지만
	// 카메라 방향은 뚫려 있는" 상황(위를 보거나 모서리를 비껴 조준)에서
	// 뷰모델이 이유 없이 당겨져 손이 어긋나 보이므로 하지 않는다
	WallHardStopOwnTargetAlpha = WallHardStopTargetAlpha;
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
	WallVeryCloseOwnTargetAlpha = WallVeryCloseTargetAlpha;

	// ── 7. 상/하 회피 라우팅 ─────────────────────────────────────────────
	// Ceiling이 뚜렷하면 무조건 아래로, 아니면 위/아래 중 우세한 쪽으로
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

	// ── 8. 좌/우 회피 ────────────────────────────────────────────────────
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

	// ── 디버그 드로잉 (bDrawWallOffsetDebug) ─────────────────────────────
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

	// ── 9. 타깃 스무딩과 보간 ────────────────────────────────────────────
	// 타깃 스무딩은 상승 시 프로브 노이즈를 가라앉히기 위한 것. 하강에도
	// 적용하면 블렌드 아웃 보간 위에 지수 감쇠 꼬리가 하나 더 쌓이므로,
	// 릴리즈 시에는 스무딩 타깃을 즉시 떨어뜨린다. 마지막으로 SnapWallAlpha가
	// 점근 구간을 잘라 오프셋이 실제로 0에 도달하게 한다.
	const auto SmoothWallTarget = [](float Smoothed, float Target, float SmoothAlpha)
	{
		return Target < Smoothed ? Target : FMath::Lerp(Smoothed, Target, SmoothAlpha);
	};
	const auto SnapWallAlpha = [](float Alpha, float Target)
	{
		return (Target <= KINDA_SMALL_NUMBER && Alpha < 0.04f) ? 0.0f : Alpha;
	};

	const float WallTargetSmoothSpeed = FMath::Max(WeaponValues->WallTargetSmoothSpeed, 0.0f);
	const float WallTargetSmoothAlpha = 1.0f - FMath::Exp(-WallTargetSmoothSpeed * DeltaSeconds);
	WallSmoothedTargetAlpha = SmoothWallTarget(WallSmoothedTargetAlpha, WallTargetAlpha, WallTargetSmoothAlpha);
	WallUpSmoothedTargetAlpha = SmoothWallTarget(WallUpSmoothedTargetAlpha, WallUpTargetAlpha, WallTargetSmoothAlpha);
	WallDownSmoothedTargetAlpha = SmoothWallTarget(WallDownSmoothedTargetAlpha, WallDownTargetAlpha, WallTargetSmoothAlpha);
	WallSideSmoothedTargetAlpha = SmoothWallTarget(WallSideSmoothedTargetAlpha, WallSideTargetAlpha, WallTargetSmoothAlpha);

	const float MuzzleBlockTargetSmoothAlpha =
		1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallMuzzleBlockTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallMuzzleBlockSmoothedTargetAlpha =
		SmoothWallTarget(WallMuzzleBlockSmoothedTargetAlpha, WallMuzzleBlockTargetAlpha, MuzzleBlockTargetSmoothAlpha);

	const float VeryCloseTargetSmoothAlpha =
		1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallVeryCloseTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallVeryCloseSmoothedTargetAlpha =
		SmoothWallTarget(WallVeryCloseSmoothedTargetAlpha, WallVeryCloseTargetAlpha, VeryCloseTargetSmoothAlpha);

	const float InterpSpeed = WallSmoothedTargetAlpha > WallOffsetAlpha
		? WeaponValues->WallBlendInSpeed
		: WeaponValues->WallBlendOutSpeed;

	WallOffsetAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallOffsetAlpha,
		WallSmoothedTargetAlpha,
		DeltaSeconds,
		InterpSpeed
	), WallSmoothedTargetAlpha);

	WallAvoidUpAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidUpAlpha,
		WallUpSmoothedTargetAlpha,
		DeltaSeconds,
		WallUpSmoothedTargetAlpha > WallAvoidUpAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallUpSmoothedTargetAlpha);
	WallAvoidDownAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidDownAlpha,
		WallDownSmoothedTargetAlpha,
		DeltaSeconds,
		WallDownSmoothedTargetAlpha > WallAvoidDownAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallDownSmoothedTargetAlpha);
	WallAvoidSideAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallAvoidSideAlpha,
		WallSideSmoothedTargetAlpha,
		DeltaSeconds,
		WallSideSmoothedTargetAlpha > WallAvoidSideAlpha ? WeaponValues->WallBlendInSpeed : WeaponValues->WallBlendOutSpeed
	), WallSideSmoothedTargetAlpha);
	WallAvoidSideSign = FMath::FInterpTo(
		WallAvoidSideSign,
		WallSideSmoothedTargetAlpha > KINDA_SMALL_NUMBER ? WallSideTargetSign : 0.0f,
		DeltaSeconds,
		WeaponValues->WallBlendInSpeed
	);
	if (WallMuzzleBlockTargetAlpha > KINDA_SMALL_NUMBER)
	{
		WallMuzzleBlockReleaseHoldTimer = FMath::Max(WeaponValues->WallMuzzleBlockReleaseHoldTime, 0.0f);
	}
	const float EffectiveMuzzleBlockTargetAlpha = WallMuzzleBlockReleaseHoldTimer > 0.0f
		? FMath::Max(WallMuzzleBlockSmoothedTargetAlpha, WallMuzzleBlockAlpha)
		: WallMuzzleBlockSmoothedTargetAlpha;
	WallMuzzleBlockAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallMuzzleBlockAlpha,
		EffectiveMuzzleBlockTargetAlpha,
		DeltaSeconds,
		EffectiveMuzzleBlockTargetAlpha > WallMuzzleBlockAlpha
			? WeaponValues->WallMuzzleBlockBlendInSpeed
			: WeaponValues->WallMuzzleBlockBlendOutSpeed
	), EffectiveMuzzleBlockTargetAlpha);
	const float HardStopTargetSmoothAlpha = 1.0f - FMath::Exp(-FMath::Max(WeaponValues->WallHardStopTargetSmoothSpeed, 0.0f) * DeltaSeconds);
	WallHardStopSmoothedTargetAlpha = SmoothWallTarget(WallHardStopSmoothedTargetAlpha, WallHardStopTargetAlpha, HardStopTargetSmoothAlpha);
	WallHardStopAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallHardStopAlpha,
		WallHardStopSmoothedTargetAlpha,
		DeltaSeconds,
		WallHardStopSmoothedTargetAlpha > WallHardStopAlpha
			? WeaponValues->WallHardStopBlendInSpeed
			: WeaponValues->WallHardStopBlendOutSpeed
	), WallHardStopSmoothedTargetAlpha);
	WallHardStopSafetyAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallHardStopSafetyAlpha,
		WallHardStopTargetAlpha,
		DeltaSeconds,
		WallHardStopTargetAlpha > WallHardStopSafetyAlpha
			? WeaponValues->WallHardStopSafetyBlendInSpeed
			: WeaponValues->WallHardStopSafetyBlendOutSpeed
	), WallHardStopTargetAlpha);
	WallVeryCloseAlpha = SnapWallAlpha(FMath::FInterpTo(
		WallVeryCloseAlpha,
		WallVeryCloseSmoothedTargetAlpha,
		DeltaSeconds,
		WallVeryCloseSmoothedTargetAlpha > WallVeryCloseAlpha
			? WeaponValues->WallVeryCloseBlendInSpeed
			: WeaponValues->WallVeryCloseBlendOutSpeed
	), WallVeryCloseSmoothedTargetAlpha);

	// ── 10. 오프셋 합성 ──────────────────────────────────────────────────
	// 단계별 당김을 합산해 최종 뷰모델 위치/회전 오프셋을 만든다.
	// 근접 단계가 강해질수록 MuzzleBlock 항은 억제되어 이중 당김을 막는다
	const float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallOffsetAlpha, 2.0f);
	const float EasedMuzzleBlockAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallMuzzleBlockAlpha, 2.0f);
	const float EasedVeryCloseAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallVeryCloseAlpha, 2.0f);
	const float EasedHardStopAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopAlpha, 2.0f);
	const float EasedHardStopSafetyAlpha = FMath::InterpEaseOut(0.0f, 1.0f, WallHardStopSafetyAlpha, 2.0f);
	const float EasedFinalHardStopAlpha = FMath::Max(EasedHardStopAlpha, EasedHardStopSafetyAlpha);
	const float ActionWallAlpha = FMath::Clamp(FMath::Max(ViewModelReloadPoseAlpha, ViewModelEquipPoseAlpha), 0.0f, 1.0f);
	const float ActionWallOffsetScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues->WallActionOffsetScale, 0.0f, 1.0f),
		ActionWallAlpha
	);
	const float ActionWallHardStopScale = FMath::Lerp(
		1.0f,
		FMath::Clamp(WeaponValues->WallActionHardStopScale, 0.0f, 1.0f),
		ActionWallAlpha
	);
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

	const FVector WallNonHardStopOffsetLoc =
		(WeaponValues->WallMaxOffsetLoc * EasedAlpha) +
		(WeaponValues->WallMaxOffsetLoc * SideCloseAlpha * FMath::Clamp(WeaponValues->WallSideCloseOffsetScale, 0.0f, 1.0f)) +
		(WeaponValues->WallMuzzleBlockLoc * EasedMuzzleBlockAlpha * MuzzleOffsetScale) +
		(WeaponValues->WallVeryCloseLoc * EasedVeryCloseAlpha);
	ViewModelWallOffsetLoc =
		(WallNonHardStopOffsetLoc * ActionWallOffsetScale) +
		(WeaponValues->WallHardStopLoc * EasedFinalHardStopAlpha * ActionWallHardStopScale);
	const float VeryCloseMuzzleRotSuppressScale = FMath::Clamp(WeaponValues->WallVeryCloseMuzzleRotSuppressScale, 0.0f, 1.0f);
	const float MuzzleRotSuppressAlpha = FMath::Max(EasedVeryCloseAlpha * VeryCloseMuzzleRotSuppressScale, EasedFinalHardStopAlpha);
	const float MuzzleRotScale = FMath::Clamp(1.0f - MuzzleRotSuppressAlpha, 0.0f, 1.0f);
	const FRotator WallNonHardStopOffsetRot =
		(WeaponValues->WallMaxOffsetRot * EasedAlpha) +
		(WeaponValues->WallSideOffsetRot * WallAvoidSideSign * EasedSideAlpha) +
		(WeaponValues->WallMuzzleBlockRot * EasedMuzzleBlockAlpha * (1.0f - WallMuzzleBlockDownPreferenceAlpha) * MuzzleRotScale) +
		(WeaponValues->WallMuzzleBlockDownRot * EasedMuzzleBlockAlpha * WallMuzzleBlockDownPreferenceAlpha * MuzzleRotScale) +
		(WeaponValues->WallVeryCloseRot * EasedVeryCloseAlpha);
	ViewModelWallOffsetRot =
		(WallNonHardStopOffsetRot * ActionWallOffsetScale) +
		(WeaponValues->WallHardStopRot * EasedFinalHardStopAlpha * ActionWallHardStopScale);
	WallOffsetLoc = ViewModelWallOffsetLoc;
	WallOffsetRot = ViewModelWallOffsetRot;
}
