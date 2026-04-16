// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "KismetAnimationLibrary.h"

void UShooterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(TryGetPawnOwner());

	if (!IsValid(Shooter))
	{
		return;
	}

	Speed = Shooter->GetCharacterMovement()->Velocity.Size2D();
	Direction = UKismetAnimationLibrary::CalculateDirection(Shooter->GetCharacterMovement()->Velocity, Shooter->GetActorRotation());
	CurrentWeaponType = Shooter->GetWeaponType();

	AimYaw = Shooter->GetAimYawForAnimation();
	AimPitch = Shooter->GetAimPitchForAnimation();

	USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	if (!OwningMesh)
	{
		return;
	}

	bIsFirstPerson = (OwningMesh == Shooter->GetFirstPersonMesh());
	LeftHandIKAlpha = bIsFirstPerson ? 0.4f : 1.0f;
	LeftHandIKTransform = FTransform::Identity;

	if (CurrentWeaponType == EWeaponType::Unarmed)
	{
		LeftHandIKAlpha = 0.0f;
	}

	AWeaponBase* CurrentWeapon = Shooter->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetWeaponByView(bIsFirstPerson);
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

	LeftHandIKTransform.SetLocation(OutLocation);
	LeftHandIKTransform.SetRotation(FQuat(OutRotation));
	LeftHandIKTransform.SetScale3D(FVector::OneVector);
}
