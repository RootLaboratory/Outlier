// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerCameraManager.h"
#include "Drone/Partner/PartnerCharacter.h"
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
	if (ShooterCharacter)
	{
		OutViewRotation.Roll = FMath::Clamp(
			ShooterCharacter->GetCurrentLeanRollDegrees(),
			-1.0f * ShooterCharacter->GetMaxLeanAngle(),
			ShooterCharacter->GetMaxLeanAngle()
		);
		return;
	}

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(OwningController->GetPawn());
	if (PartnerCharacter)
	{
		const float InertialRoll = PartnerCharacter->GetCurrentInertialCameraRollDegrees();
		const float MaxInertialRoll = PartnerCharacter->GetMaxInertialCameraRollDegrees();

		OutViewRotation.Roll = FMath::Clamp(
			InertialRoll,
			-1.0f * MaxInertialRoll,
			MaxInertialRoll
		);
		return;
	}

	OutViewRotation.Roll = 0.0f;
}

void AFirstPersonPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	Super::UpdateViewTarget(OutVT, DeltaTime);

	const APlayerController* OwningController = PCOwner;
	if (!OwningController)
	{
		return;
	}

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(OwningController->GetPawn());
	if (!PartnerCharacter)
	{
		return;
	}

	const float InertialPitch = FMath::Clamp(
		PartnerCharacter->GetCurrentInertialCameraPitchDegrees(),
		-1.0f * PartnerCharacter->GetMaxInertialCameraPitchDegrees(),
		PartnerCharacter->GetMaxInertialCameraPitchDegrees()
	);

	const FRotator ViewRotationBeforeInertialTilt = OutVT.POV.Rotation;
	OutVT.POV.Rotation.Pitch = FMath::ClampAngle(
		ViewRotationBeforeInertialTilt.Pitch + InertialPitch,
		ViewPitchMin,
		ViewPitchMax
	);
}
