// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/VECDrone.h"
#include "GameFramework/PlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "Outlier.h"

AFirstPersonPlayerCameraManager::AFirstPersonPlayerCameraManager()
{
	// set the min/max pitch
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}

void AFirstPersonPlayerCameraManager::PlayExplosionCameraShake(
	TSubclassOf<UCameraShakeBase> CameraShakeClass,
	float Scale,
	bool bAllowInactivePawn)
{
	if (!PCOwner || !PCOwner->IsLocalController() || !CameraShakeClass)
	{
		return;
	}

	const APawn* ControlledPawn = PCOwner->GetPawn();
	if (!bAllowInactivePawn && (!ControlledPawn || GetViewTarget() != ControlledPawn))
	{
		return;
	}

	const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (WorldTime >= ActiveExplosionShakeEndTime)
	{
		ActiveExplosionShake.Reset();
		ActiveExplosionShakeScale = 0.0f;
	}

	const float FinalScale = FMath::Clamp(
		Scale * MasterCameraMotionScale * ExplosionShakeScale,
		0.0f,
		MaxExplosionShakeScale);
	if (FinalScale <= 0.0f
		|| WorldTime - LastExplosionShakeStartTime < ExplosionShakeRestartCooldownSeconds
		|| (ActiveExplosionShake.IsValid() && FinalScale <= ActiveExplosionShakeScale))
	{
		return;
	}

	if (UCameraShakeBase* ExistingShake = ActiveExplosionShake.Get())
	{
		StopCameraShake(ExistingShake, true);
	}

	ActiveExplosionShake = StartCameraShake(
		CameraShakeClass,
		FinalScale,
		ECameraShakePlaySpace::CameraLocal,
		FRotator::ZeroRotator);
	ActiveExplosionShakeScale = FinalScale;
	LastExplosionShakeStartTime = WorldTime;
	ActiveExplosionShakeEndTime = WorldTime + ExplosionShakeDurationSeconds;

	UE_LOG(
		LogOutlier,
		Verbose,
		TEXT("[CameraShake] Explosion shake started. Controller=%s Scale=%.3f Class=%s"),
		*GetNameSafe(PCOwner),
		FinalScale,
		*GetNameSafe(CameraShakeClass.Get()));
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
		const float LeanRoll = ShooterCharacter->GetCurrentLeanRollDegrees();
		const float SlideRoll = ShooterCharacter->GetCurrentSlideCameraRollDegrees();
		OutViewRotation.Roll = FMath::Clamp(
			LeanRoll + SlideRoll,
			-1.0f * ShooterCharacter->GetMaxLeanAngle(),
			ShooterCharacter->GetMaxLeanAngle()
		);
		return;
	}

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(OwningController->GetPawn());
	if (PartnerCharacter)
	{
		const float MotionScale = MasterCameraMotionScale * MovementTiltScale;
		const float InertialRoll = PartnerCharacter->GetCurrentInertialCameraRollDegrees() * MotionScale;
		const float MaxInertialRoll = PartnerCharacter->GetMaxInertialCameraRollDegrees();

		OutViewRotation.Roll = FMath::Clamp(
			InertialRoll,
			-1.0f * MaxInertialRoll,
			MaxInertialRoll
		);
		return;
	}

	const AVECDrone* VECDrone = Cast<AVECDrone>(OwningController->GetPawn());
	if (VECDrone)
	{
		const float MotionScale = MasterCameraMotionScale * MovementTiltScale;
		const float DroneRoll = VECDrone->GetCurrentCameraRollDegrees() * MotionScale;
		const float MaxDroneRoll = VECDrone->GetMaxCameraRollDegrees();

		OutViewRotation.Roll = FMath::Clamp(
			DroneRoll,
			-1.0f * MaxDroneRoll,
			MaxDroneRoll
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

	const AVECDrone* VECDrone = Cast<AVECDrone>(OwningController->GetPawn());
	if (VECDrone)
	{
		const float MaxDronePitch = VECDrone->GetMaxCameraPitchDegrees();
		const float InertialPitch = FMath::Clamp(
			VECDrone->GetCurrentCameraPitchDegrees() * MasterCameraMotionScale * MovementTiltScale,
			-1.0f * MaxDronePitch,
			MaxDronePitch
		);
		OutVT.POV.Rotation.Pitch = FMath::ClampAngle(
			OutVT.POV.Rotation.Pitch + InertialPitch,
			ViewPitchMin,
			ViewPitchMax
		);
		return;
	}

	const APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(OwningController->GetPawn());
	if (!PartnerCharacter)
	{
		return;
	}

	const float InertialPitch = FMath::Clamp(
		PartnerCharacter->GetCurrentInertialCameraPitchDegrees() * MasterCameraMotionScale * MovementTiltScale,
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
