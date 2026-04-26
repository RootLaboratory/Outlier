// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Weapon/WeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterFirstPersonAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UShooterFirstPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	UFUNCTION()
	void HandleOwnerDeath();
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
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float FirstPersonAimPitchScale = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float FirstPersonAimPitchClamp = 35.0f;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsCrouching : 1 = false;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsGrounded : 1 = true;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsInAir : 1 = false;

	UPROPERTY(Transient)
	uint8 bHasLoggedInitialization : 1 = false;

	UPROPERTY(Transient)
	uint8 bHasLoggedMissingOwner : 1 = false;

	UPROPERTY(Transient)
	float LastLoggedSpeed = -1.0f;

	UPROPERTY(Transient)
	EWeaponType LastLoggedWeaponType = EWeaponType::Unarmed;

	UPROPERTY(Transient)
	uint8 bLastLoggedInAir : 1 = false;

	UPROPERTY(Transient)
	uint8 bLastLoggedAiming : 1 = false;

	UPROPERTY(Transient)
	uint8 bLastLoggedReloading : 1 = false;
};
