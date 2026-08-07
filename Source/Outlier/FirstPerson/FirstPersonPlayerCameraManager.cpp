// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/VECDrone.h"
#include "GameFramework/PlayerController.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "Weapon/OutlierWeaponCameraShake.h"
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

	// 폭발 피드백은 총기 진동보다 우선한다. 합산 상한을 넘으면 짧은 총기 Shake를 먼저 정리한다.
	if (ActiveWeaponShake.IsValid()
		&& FinalScale + ActiveWeaponShakeScale > MaxCombinedShakeScale)
	{
		StopWeaponCameraShake(true);
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

void AFirstPersonPlayerCameraManager::PlayWeaponCameraShake(
	float Scale,
	float DurationSeconds,
	APawn* SourcePawn)
{
	if (!PCOwner
		|| !PCOwner->IsLocalController()
		|| !SourcePawn
		|| PCOwner->GetPawn() != SourcePawn
		|| GetViewTarget() != SourcePawn)
	{
		return;
	}

	const float WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (WorldTime >= ActiveExplosionShakeEndTime)
	{
		ActiveExplosionShake.Reset();
		ActiveExplosionShakeScale = 0.0f;
	}

	const float RemainingCombinedScale = FMath::Max(
		MaxCombinedShakeScale - ActiveExplosionShakeScale,
		0.0f);
	const float FinalScale = FMath::Clamp(
		Scale * MasterCameraMotionScale * WeaponShakeScale,
		0.0f,
		FMath::Min(MaxWeaponShakeScale, RemainingCombinedScale));
	const float FinalDuration = FMath::Clamp(DurationSeconds, 0.0f, 0.08f);
	if (FinalScale <= 0.0f || FinalDuration <= 0.0f)
	{
		return;
	}

	// 단일 인스턴스 Shake는 StartCameraShake를 다시 호출하면 엔진이 기존 패턴을 재시작한다.
	// 매 발 제거 후 재생하면 CameraModifier 갱신 사이에 한 프레임 공백이 생길 수 있으므로 직접 정지하지 않는다.
	ActiveWeaponShake = StartCameraShake(
		UOutlierWeaponCameraShake::StaticClass(),
		FinalScale,
		ECameraShakePlaySpace::CameraLocal,
		FRotator::ZeroRotator);
	if (UOutlierWeaponCameraShake* WeaponShake =
		Cast<UOutlierWeaponCameraShake>(ActiveWeaponShake.Get()))
	{
		// 이전 인스턴스가 발사 사이에 종료됐더라도 매 발 다른 위상으로 시작하도록 순번을 외부에서 유지한다.
		WeaponShake->SetImpactSequence(++WeaponShakeSequence);
	}
	WeaponShakeSourcePawn = SourcePawn;
	ActiveWeaponShakeScale = FinalScale;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			WeaponShakeTimerHandle,
			this,
			&AFirstPersonPlayerCameraManager::HandleWeaponShakeFinished,
			FinalDuration,
			false);
	}
}

void AFirstPersonPlayerCameraManager::StopWeaponCameraShake(bool bImmediately)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(WeaponShakeTimerHandle);
	}

	if (UCameraShakeBase* ExistingShake = ActiveWeaponShake.Get())
	{
		StopCameraShake(ExistingShake, bImmediately);
	}

	ActiveWeaponShake.Reset();
	WeaponShakeSourcePawn.Reset();
	ActiveWeaponShakeScale = 0.0f;
}

void AFirstPersonPlayerCameraManager::HandleWeaponShakeFinished()
{
	StopWeaponCameraShake(false);
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

	if (ActiveWeaponShake.IsValid()
		&& (!WeaponShakeSourcePawn.IsValid()
			|| WeaponShakeSourcePawn.Get() != OwningController->GetPawn()
			|| WeaponShakeSourcePawn.Get() != GetViewTarget()))
	{
		StopWeaponCameraShake(true);
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
