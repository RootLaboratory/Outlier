// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Shooter/ShooterCharacter.h"

AFirstPersonPlayerCameraManager::AFirstPersonPlayerCameraManager()
{
	// set the min/max pitch
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}

void AFirstPersonPlayerCameraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot)
{
	Super::ProcessViewRotation(DeltaTime, OutViewRotation, OutDeltaRot);

	const APlayerController* OwningController = PCOwner;
	if (!OwningController)
	{
		return;
	}

	const AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(OwningController->GetPawn());
	if (!ShooterCharacter)
	{
		return;
	}

	OutViewRotation.Roll = FMath::Clamp(ShooterCharacter->GetCurrentLeanRollDegrees(), -1 * ShooterCharacter->GetMaxLeanAngle(), ShooterCharacter->GetMaxLeanAngle());
}
