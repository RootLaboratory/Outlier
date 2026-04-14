// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/ShooterAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "KismetAnimationLibrary.h"

void UShooterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(TryGetPawnOwner());

	if (IsValid(Shooter))
	{
		Speed			  = Shooter->GetCharacterMovement()->Velocity.Size2D();
		Direction		  = UKismetAnimationLibrary::CalculateDirection(Shooter->GetCharacterMovement()->Velocity, Shooter->GetActorRotation());
		CurrentWeaponType = Shooter->GetWeaponType();

		AimYaw   = Shooter->GetBaseAimRotation().Yaw;
		AimPitch = Shooter->GetBaseAimRotation().Pitch;
	}
}
