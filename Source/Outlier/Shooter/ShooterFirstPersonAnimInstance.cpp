// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

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
		return;
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
		return;
	}

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
