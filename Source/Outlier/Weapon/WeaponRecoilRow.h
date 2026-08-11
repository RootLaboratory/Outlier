// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeaponRecoilRow.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponRecoilRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control")
	float ControlPitchPerShot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control", meta = (ClampMin = "0.0"))
	float ControlPitchRandomRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control", meta = (ClampMin = "0.0"))
	float ControlYawPerShot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control", meta = (ClampMin = "0.0"))
	float ControlYawRandomRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float YawDirectionPersistence = 0.65f;

	// 높은 값일수록 계산된 반동을 짧고 즉각적으로 ControlRotation에 반영한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Control", meta = (ClampMin = "0.0"))
	float ControlKickInterpSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Accumulation", meta = (ClampMin = "0.0"))
	float RecoilGrowthPerShot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Accumulation", meta = (ClampMin = "1.0"))
	float MaxRecoilMultiplier = 1.0f;

	// 한 연사 구간에서 누적할 수 있는 최대 상향 반동 각도다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Accumulation", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "Degrees"))
	float MaxAccumulatedPitchDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Accumulation", meta = (ClampMin = "0.0"))
	float MaxAccumulatedYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Accumulation", meta = (ClampMin = "0.0"))
	float RecoilResetDelay = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Camera", meta = (ClampMin = "0.0"))
	float CameraShakeDuration = 0.06f;
};
