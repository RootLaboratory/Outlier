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
		CurrentWeaponType = EWeaponType::Unarmed;
		bIsCrouching = false;
		bIsSprinting = false;
		bIsSliding = false;
		bIsGrounded = true;
		bIsInAir = false;
		bIsAiming = false;
		bIsReloading = false;
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

	Speed = CharacterMovement->Velocity.Size2D();
	Direction = UKismetAnimationLibrary::CalculateDirection(
		CharacterMovement->Velocity,
		CachedShooterCharacter->GetActorRotation()
	);
	CurrentWeaponType = CachedShooterCharacter->GetWeaponType();

	bIsCrouching = CachedShooterCharacter->bIsCrouched;
	bIsSprinting = CachedShooterCharacter->IsSprinting();
	bIsSliding = CachedShooterCharacter->IsSliding();
	bIsGrounded = CharacterMovement->IsMovingOnGround();
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
