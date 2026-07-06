// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"

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
		ResetThirdPersonProceduralRuntime();
		return;
	}

	UCharacterMovementComponent* CharacterMovement = CachedShooterCharacter->GetCharacterMovement();
	if (!CharacterMovement)
	{
		Speed = 0.0f;
		Direction = 0.0f;
		bIsGrounded = true;
		bIsInAir = false;
		ResetThirdPersonProceduralRuntime();
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

	USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	AWeaponBase* CurrentWeapon = CachedShooterCharacter->GetCurrentWeapon();
	bool bHasLeftHandIK = false;

	if (IsFirearmWeapon(CurrentWeaponType) && OwningMesh && CurrentWeapon)
	{
		USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetWeaponByView(false);
		if (WeaponMesh)
		{
			const FName SocketName = CurrentWeapon->GetLeftHandIKSocketName();
			if (WeaponMesh->DoesSocketExist(SocketName))
			{
				const FTransform SocketWorldTransform = WeaponMesh->GetSocketTransform(SocketName, RTS_World);

				FVector OutLocation = FVector::ZeroVector;
				FRotator OutRotation = FRotator::ZeroRotator;

				OwningMesh->TransformToBoneSpace(
					FName("hand_r"),
					SocketWorldTransform.GetLocation(),
					SocketWorldTransform.Rotator(),
					OutLocation,
					OutRotation
				);

				if (CurrentWeaponType == EWeaponType::Rifle)
				{
					if (FMath::Abs(AimPitch) > LeftHandIKRiflePitchOffsetStart)
					{
						const float NormalizedPitchAlpha = FMath::GetMappedRangeValueClamped(
							FVector2D(LeftHandIKRiflePitchOffsetStart, 90.0f),
							FVector2D(0.0f, 1.0f),
							FMath::Abs(AimPitch)
						);

						const FVector PitchOffset = AimPitch >= 0.0f
							? (LeftHandIKRiflePitchOffsetAtMaxUp * NormalizedPitchAlpha)
							: (LeftHandIKRiflePitchOffsetAtMaxDown * NormalizedPitchAlpha);

						OutLocation += PitchOffset;
					}
				}
				else if (CurrentWeaponType == EWeaponType::Pistol)
				{
					if (FMath::Abs(AimPitch) > LeftHandIKPistolPitchOffsetStart)
					{
						const float NormalizedPitchAlpha = FMath::GetMappedRangeValueClamped(
							FVector2D(LeftHandIKPistolPitchOffsetStart, 90.0f),
							FVector2D(0.0f, 1.0f),
							FMath::Abs(AimPitch)
						);

						const FVector PitchOffset = AimPitch >= 0.0f
							? (LeftHandIKPistolPitchOffsetAtMaxUp * NormalizedPitchAlpha)
							: (LeftHandIKPistolPitchOffsetAtMaxDown * NormalizedPitchAlpha);

						OutLocation += PitchOffset;
					}
				}

				LeftHandIKTransform.SetLocation(OutLocation);
				LeftHandIKTransform.SetRotation(FQuat(OutRotation));
				LeftHandIKTransform.SetScale3D(FVector::OneVector);
				bHasLeftHandIK = true;
			}
		}
	}

	if (!bHasLeftHandIK)
	{
		LeftHandIKTransform = FTransform::Identity;
	}

	UpdateThirdPersonProceduralRuntime(DeltaSeconds, IsFirearmWeapon(CurrentWeaponType) && CurrentWeapon != nullptr, bHasLeftHandIK);
}

void UShooterAnimInstance::HandleOwnerDeath()
{
	bIsDead = true;
}

void UShooterAnimInstance::HandleOwnerMovementStateChanged(EMovementState NewState)
{
	MovementState = NewState;
}

void UShooterAnimInstance::ResetThirdPersonProceduralRuntime()
{
	TurnInPlaceYaw = 0.0f;
	TurnInPlaceDirection = 0.0f;
	TurnInPlaceVisualYaw = 0.0f;
	PreviousBaseAimYaw = 0.0f;
	TurnInPlaceResetTimeRemaining = 0.0f;
	bHasPreviousBaseAimYaw = false;
	bIsTurnInPlaceConsuming = false;
	bTurnInPlaceResetRequested = false;
	ThirdPersonProceduralRuntime.Reset();
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

void UShooterAnimInstance::UpdateThirdPersonProceduralRuntime(float DeltaSeconds, bool bHasWeapon, bool bHasLeftHandIK)
{
	const bool bCanUseWeaponProcedural = bHasWeapon && IsFirearmWeapon(CurrentWeaponType) && !bIsDead;
	const bool bFireState = CombatState == ECombatState::Fire;
	const bool bAimIntent = bCanUseWeaponProcedural && (bIsAiming || bFireState) && !bIsReloading;
	const bool bUpperBodyIntent = bCanUseWeaponProcedural && (bAimIntent || bIsReloading || bIsSliding);

	ThirdPersonProceduralRuntime.AimYaw = AimYaw;
	ThirdPersonProceduralRuntime.AimPitch = AimPitch;
	ThirdPersonProceduralRuntime.bIsCrouching = bIsCrouching;
	ThirdPersonProceduralRuntime.bIsAiming = bIsAiming;
	ThirdPersonProceduralRuntime.bIsReloading = bIsReloading;
	ThirdPersonProceduralRuntime.AimAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.AimAlpha,
		bAimIntent ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonAimBlendInSpeed,
		ThirdPersonAimBlendOutSpeed
	);
	ThirdPersonProceduralRuntime.UpperBodyAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.UpperBodyAlpha,
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
		ThirdPersonProceduralRuntime.TurnInPlaceAlpha = 0.0f;
	}
	else
	{
		ThirdPersonProceduralRuntime.TurnInPlaceAlpha = InterpAnimAlpha(
			ThirdPersonProceduralRuntime.TurnInPlaceAlpha,
			TurnTargetAlpha,
			DeltaSeconds,
			ThirdPersonTurnInPlaceBlendInSpeed,
			ThirdPersonTurnInPlaceBlendOutSpeed
		);
	}

	TurnInPlaceYaw = TurnYaw;
	ThirdPersonProceduralRuntime.TurnInPlaceYaw = TurnInPlaceYaw;

	if (bTurnInPlaceResetRequested || !bCanTurnInPlace || (!bIsTurnInPlaceConsuming && TurnTargetAlpha <= KINDA_SMALL_NUMBER))
	{
		TurnInPlaceDirection = 0.0f;
	}
	else
	{
		TurnInPlaceDirection = TurnYaw >= 0.0f ? 1.0f : -1.0f;
	}

	ThirdPersonProceduralRuntime.TurnInPlaceDirection = TurnInPlaceDirection;

	//UE_LOG(LogTemp, Warning, TEXT("Yaw : %f, Direction : %f"), TurnInPlaceYaw, TurnInPlaceDirection);

	ThirdPersonProceduralRuntime.TurnInPlacePlayRate = FMath::GetMappedRangeValueClamped(
		FVector2D(ThirdPersonTurnInPlaceStartYaw, ThirdPersonTurnInPlaceFullYaw),
		FVector2D(ThirdPersonTurnInPlaceMinPlayRate, ThirdPersonTurnInPlaceMaxPlayRate),
		AbsTurnYaw
	);

	ThirdPersonProceduralRuntime.LeanAlpha = LeanAlpha;
	ThirdPersonProceduralRuntime.LeftHandIKTransform = LeftHandIKTransform;
	ThirdPersonProceduralRuntime.LeftHandIKAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.LeftHandIKAlpha,
		bCanUseWeaponProcedural && bHasLeftHandIK && !bIsReloading ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonLeftHandIKBlendInSpeed,
		ThirdPersonLeftHandIKBlendOutSpeed
	);
	ThirdPersonProceduralRuntime.FireAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.FireAlpha,
		bCanUseWeaponProcedural && bFireState ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonProceduralRuntime.ReloadAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.ReloadAlpha,
		bCanUseWeaponProcedural && bIsReloading ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonProceduralRuntime.SprintAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.SprintAlpha,
		bCanUseWeaponProcedural && bIsSprinting ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
	ThirdPersonProceduralRuntime.SlideAlpha = InterpAnimAlpha(
		ThirdPersonProceduralRuntime.SlideAlpha,
		bCanUseWeaponProcedural && bIsSliding ? 1.0f : 0.0f,
		DeltaSeconds,
		ThirdPersonActionBlendInSpeed,
		ThirdPersonActionBlendOutSpeed
	);
}
