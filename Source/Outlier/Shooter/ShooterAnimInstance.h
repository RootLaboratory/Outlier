// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Shooter/Anim/ThirdPersonProceduralAnimRuntime.h"
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

	void ResetThirdPersonProceduralRuntime();
	void UpdateThirdPersonProceduralRuntime(float DeltaSeconds, bool bHasWeapon, bool bHasLeftHandIK);
	float UpdateTurnInPlaceYaw(float DeltaSeconds, bool bCanTurnInPlace);

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

	UPROPERTY(Transient)
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceVisualYaw = 0.0f;

	float PreviousBaseAimYaw = 0.0f;
	float TurnInPlaceResetTimeRemaining = 0.0f;
	uint8 bHasPreviousBaseAimYaw : 1 = false;
	uint8 bIsTurnInPlaceConsuming : 1 = false;
	uint8 bTurnInPlaceResetRequested : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim|TP Procedural")
	FThirdPersonProceduralAnimRuntime ThirdPersonProceduralRuntime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Aim", meta = (ClampMin = "0.0"))
	float ThirdPersonAimBlendInSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Aim", meta = (ClampMin = "0.0"))
	float ThirdPersonAimBlendOutSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Upper Body", meta = (ClampMin = "0.0"))
	float ThirdPersonUpperBodyBlendInSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Upper Body", meta = (ClampMin = "0.0"))
	float ThirdPersonUpperBodyBlendOutSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMinSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceStartYaw = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceFullYaw = 115.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceYawConsumeSpeed = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceResetHoldTime = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceBlendInSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceBlendOutSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMinPlayRate = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMaxPlayRate = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Left Hand IK", meta = (ClampMin = "0.0"))
	float ThirdPersonLeftHandIKBlendInSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Left Hand IK", meta = (ClampMin = "0.0"))
	float ThirdPersonLeftHandIKBlendOutSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Action", meta = (ClampMin = "0.0"))
	float ThirdPersonActionBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Action", meta = (ClampMin = "0.0"))
	float ThirdPersonActionBlendOutSpeed = 14.0f;

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
