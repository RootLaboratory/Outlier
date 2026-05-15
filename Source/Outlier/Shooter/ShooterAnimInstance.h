// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Weapon/WeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UShooterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	UFUNCTION()
	void HandleOwnerDeath();

	UFUNCTION()
	void HandleOwnerMovementStateChanged(EMovementState NewState);

protected:
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

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	float LeanAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSliding : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsDead : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	EMovementState MovementState = EMovementState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	ECombatState CombatState = ECombatState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	EWeaponMode WeaponMode = EWeaponMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSlidingCanceled : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsGrounded : 1 = true;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsInAir : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsPrimaryWeapon : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsSecondaryWeapon : 1 = false;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = IK)
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	float LeftHandIKRiflePitchOffsetStart = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	FVector LeftHandIKRiflePitchOffsetAtMaxUp = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	FVector LeftHandIKRiflePitchOffsetAtMaxDown = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	float LeftHandIKPistolPitchOffsetStart = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	FVector LeftHandIKPistolPitchOffsetAtMaxUp = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	FVector LeftHandIKPistolPitchOffsetAtMaxDown = FVector(0.0f, 0.0f, 0.0f);
};
