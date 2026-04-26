// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "OutlierNetUtils.h"

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
			TEXT("%s [FPAnim] Init AnimClass=%s Mesh=%s Owner=%s WeaponType=%d OnlyOwnerSee=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			*GetClass()->GetName(),
			*GetNameSafe(OwningMesh),
			*GetNameSafe(CachedShooterCharacter),
			static_cast<int32>(CachedShooterCharacter->GetWeaponType()),
			OwningMesh->GetOnlyOwnerSee() ? 1 : 0);
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
		if (!bHasLoggedMissingOwner)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FPAnim] NativeUpdateAnimation missing CachedShooterCharacter OwnerPawn=%s"), *GetNameSafe(TryGetPawnOwner()));
			bHasLoggedMissingOwner = true;
		}
		return;
	}

	bHasLoggedMissingOwner = false;

	Speed = CachedShooterCharacter->GetCharacterMovement()->Velocity.Size2D();
	Direction = UKismetAnimationLibrary::CalculateDirection(
		CachedShooterCharacter->GetCharacterMovement()->Velocity,
		CachedShooterCharacter->GetActorRotation()
	);
	CurrentWeaponType = CachedShooterCharacter->GetWeaponType();

	bIsCrouching = CachedShooterCharacter->bIsCrouched;
	bIsSprinting = CachedShooterCharacter->IsSprinting();
	bIsSliding = CachedShooterCharacter->IsSliding();
	bIsGrounded = CachedShooterCharacter->GetCharacterMovement()->IsMovingOnGround();
	bIsInAir = !bIsGrounded;
	bIsAiming = CachedShooterCharacter->IsAiming();
	bIsReloading = CachedShooterCharacter->IsReloading();
	bIsDead = CachedShooterCharacter->IsDead();

	if (!bHasLoggedInitialization)
	{
		if (USkeletalMeshComponent* OwningMesh = GetOwningComponent())
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("%s [FPAnim] FirstUpdate AnimClass=%s Mesh=%s Owner=%s Speed=%.2f WeaponType=%d InAir=%d Aiming=%d Reloading=%d"),
				OutlierNet::GetNetPrefix(CachedShooterCharacter),
				*GetClass()->GetName(),
				*GetNameSafe(OwningMesh),
				*GetNameSafe(CachedShooterCharacter),
				Speed,
				static_cast<int32>(CurrentWeaponType),
				bIsInAir ? 1 : 0,
				bIsAiming ? 1 : 0,
				bIsReloading ? 1 : 0);
		}

		bHasLoggedInitialization = true;
		LastLoggedSpeed = Speed;
		LastLoggedWeaponType = CurrentWeaponType;
		bLastLoggedInAir = bIsInAir;
		bLastLoggedAiming = bIsAiming;
		bLastLoggedReloading = bIsReloading;
	}
	else if (FMath::Abs(Speed - LastLoggedSpeed) > 10.0f
		|| CurrentWeaponType != LastLoggedWeaponType
		|| bIsInAir != bLastLoggedInAir
		|| bIsAiming != bLastLoggedAiming
		|| bIsReloading != bLastLoggedReloading)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s [FPAnim] State Speed=%.2f WeaponType=%d InAir=%d Aiming=%d Reloading=%d Sliding=%d Sprinting=%d Crouching=%d"),
			OutlierNet::GetNetPrefix(CachedShooterCharacter),
			Speed,
			static_cast<int32>(CurrentWeaponType),
			bIsInAir ? 1 : 0,
			bIsAiming ? 1 : 0,
			bIsReloading ? 1 : 0,
			bIsSliding ? 1 : 0,
			bIsSprinting ? 1 : 0,
			bIsCrouching ? 1 : 0);

		LastLoggedSpeed = Speed;
		LastLoggedWeaponType = CurrentWeaponType;
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
	AimPitch = CachedShooterCharacter->GetAimPitchForAnimation() * FirstPersonAimPitchScale;

	AimPitch = FMath::Clamp(AimPitch, -FirstPersonAimPitchClamp, FirstPersonAimPitchClamp);
}

void UShooterFirstPersonAnimInstance::HandleOwnerDeath()
{
	bIsDead = true;
}
