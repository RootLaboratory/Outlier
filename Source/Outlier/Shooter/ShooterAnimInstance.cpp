// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"

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
			TEXT("%s [TPAnim] Init AnimClass=%s Mesh=%s Owner=%s WeaponType=%d OwnerNoSee=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			*GetClass()->GetName(),
			*GetNameSafe(OwningMesh),
			*GetNameSafe(CachedShooterCharacter),
			static_cast<int32>(CachedShooterCharacter->GetWeaponType()),
			OwningMesh->GetOwnerNoSee() ? 1 : 0);
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

	if(!CachedShooterCharacter)
	{
		if (!bHasLoggedMissingOwner)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TPAnim] NativeUpdateAnimation missing CachedShooterCharacter OwnerPawn=%s"), *GetNameSafe(TryGetPawnOwner()));
			bHasLoggedMissingOwner = true;
		}
		return;
	}

	bHasLoggedMissingOwner = false;

	Speed		      = CachedShooterCharacter->GetCharacterMovement()->Velocity.Size2D();
	Direction		  = UKismetAnimationLibrary::CalculateDirection(
		CachedShooterCharacter->GetCharacterMovement()->Velocity,
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
	bIsGrounded		   = CachedShooterCharacter->GetCharacterMovement()->IsMovingOnGround();
	bIsInAir		   = !bIsGrounded;
	bIsAiming	       = CachedShooterCharacter->IsAiming();
	bIsReloading       = CachedShooterCharacter->IsReloading();
	bIsDead		       = CachedShooterCharacter->IsDead();
	bIsPrimaryWeapon   = (WeaponMode == EWeaponMode::Primary);
	bIsSecondaryWeapon = (WeaponMode == EWeaponMode::Secondary);

	if (!bHasLoggedInitialization)
	{
		if (USkeletalMeshComponent* OwningMesh = GetOwningComponent())
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("%s [TPAnim] FirstUpdate AnimClass=%s Mesh=%s Owner=%s Speed=%.2f WeaponType=%d Move=%d Combat=%d InAir=%d Aiming=%d Reloading=%d"),
				OutlierNet::GetNetPrefix(CachedShooterCharacter),
				*GetClass()->GetName(),
				*GetNameSafe(OwningMesh),
				*GetNameSafe(CachedShooterCharacter),
				Speed,
				static_cast<int32>(CurrentWeaponType),
				static_cast<int32>(MovementState),
				static_cast<int32>(CombatState),
				bIsInAir ? 1 : 0,
				bIsAiming ? 1 : 0,
				bIsReloading ? 1 : 0);
		}

		bHasLoggedInitialization = true;
		LastLoggedSpeed = Speed;
		LastLoggedWeaponType = CurrentWeaponType;
		LastLoggedMovementState = MovementState;
		LastLoggedCombatState = CombatState;
		bLastLoggedInAir = bIsInAir;
		bLastLoggedAiming = bIsAiming;
		bLastLoggedReloading = bIsReloading;
	}
	else if (FMath::Abs(Speed - LastLoggedSpeed) > 10.0f
		|| CurrentWeaponType != LastLoggedWeaponType
		|| MovementState != LastLoggedMovementState
		|| CombatState != LastLoggedCombatState
		|| bIsInAir != bLastLoggedInAir
		|| bIsAiming != bLastLoggedAiming
		|| bIsReloading != bLastLoggedReloading)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s [TPAnim] State Speed=%.2f WeaponType=%d Move=%d Combat=%d InAir=%d Aiming=%d Reloading=%d Sliding=%d Sprinting=%d Crouching=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			Speed,
			static_cast<int32>(CurrentWeaponType),
			static_cast<int32>(MovementState),
			static_cast<int32>(CombatState),
			bIsInAir ? 1 : 0,
			bIsAiming ? 1 : 0,
			bIsReloading ? 1 : 0,
			bIsSliding ? 1 : 0,
			bIsSprinting ? 1 : 0,
			bIsCrouching ? 1 : 0);

		LastLoggedSpeed = Speed;
		LastLoggedWeaponType = CurrentWeaponType;
		LastLoggedMovementState = MovementState;
		LastLoggedCombatState = CombatState;
		bLastLoggedInAir = bIsInAir;
		bLastLoggedAiming = bIsAiming;
		bLastLoggedReloading = bIsReloading;
	}

	if (CurrentWeaponType != EWeaponType::Rifle && CurrentWeaponType != EWeaponType::Pistol)
	{
		return;
	}

	USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	if (!OwningMesh)
	{
		return;
	}

	AimYaw = CachedShooterCharacter->GetAimYawForAnimation();
	AimPitch = CachedShooterCharacter->GetAimPitchForAnimation();

	AWeaponBase* CurrentWeapon = CachedShooterCharacter->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetWeaponByView(false);
	if (!WeaponMesh)
	{
		return;
	}

	const FName SocketName = CurrentWeapon->GetLeftHandIKSocketName();
	if (!WeaponMesh->DoesSocketExist(SocketName))
	{
		return;
	}

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
}

void UShooterAnimInstance::HandleOwnerDeath()
{
	bIsDead = true;
}

void UShooterAnimInstance::HandleOwnerMovementStateChanged(EMovementState NewState)
{
	MovementState = NewState;
}
