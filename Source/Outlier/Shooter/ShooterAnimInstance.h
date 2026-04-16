// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Weapon/WeaponBase.h"
#include "ShooterAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UShooterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Speed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Direction = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimYaw = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimPitch = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = IK)
	uint8 bIsFirstPerson : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = IK)
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	float LeftHandIKAlpha = 1.0f;
};
