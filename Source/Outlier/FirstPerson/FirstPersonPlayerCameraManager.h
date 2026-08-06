// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "FirstPersonPlayerCameraManager.generated.h"

class UCameraShakeBase;

/**
 *  Basic First Person camera manager.
 *  Limits min/max look pitch.
 */
UCLASS()
class OUTLIER_API AFirstPersonPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
	
public:
	AFirstPersonPlayerCameraManager();

	void PlayExplosionCameraShake(
		TSubclassOf<UCameraShakeBase> CameraShakeClass,
		float Scale,
		bool bAllowInactivePawn);

	UFUNCTION(BlueprintCallable, Category = "Camera|Motion")
	void SetMasterCameraMotionScale(float NewScale) { MasterCameraMotionScale = FMath::Max(NewScale, 0.0f); }

	UFUNCTION(BlueprintCallable, Category = "Camera|Motion")
	void SetMovementTiltScale(float NewScale) { MovementTiltScale = FMath::Max(NewScale, 0.0f); }

	UFUNCTION(BlueprintCallable, Category = "Camera|Motion")
	void SetExplosionShakeScale(float NewScale) { ExplosionShakeScale = FMath::Max(NewScale, 0.0f); }

protected:
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Motion", meta = (ClampMin = "0.0"))
	float MasterCameraMotionScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Motion", meta = (ClampMin = "0.0"))
	float MovementTiltScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Motion", meta = (ClampMin = "0.0"))
	float ExplosionShakeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Explosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxExplosionShakeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Explosion", meta = (ClampMin = "0.0"))
	float ExplosionShakeRestartCooldownSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Explosion", meta = (ClampMin = "0.0"))
	float ExplosionShakeDurationSeconds = 0.35f;

private:
	TWeakObjectPtr<UCameraShakeBase> ActiveExplosionShake;
	float ActiveExplosionShakeScale = 0.0f;
	float ActiveExplosionShakeEndTime = 0.0f;
	float LastExplosionShakeStartTime = -BIG_NUMBER;
};
