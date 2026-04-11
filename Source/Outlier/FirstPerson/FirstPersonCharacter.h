// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FirstPersonCharacter.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
class AWeaponBase;

UCLASS()
class OUTLIER_API AFirstPersonCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn Mesh : first person view(arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First Person Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCamera;

protected:

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* LookAction;

	/** Attack Weapon Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* AttackAction;

	UPROPERTY(EditAnywhere, Category = Weapon)
	AWeaponBase* CurrentWeapon;

public:
	/** Sets default values for this character's properties */
	AFirstPersonCharacter();

protected:

	void TryStartAttack();

	void TryStopAttack();

	void MoveInput(const FInputActionValue& Value);

	void LookInput(const FInputActionValue& Value);

	void DoMove(float Right, float Forward);

	void DoAim(float Yaw, float Pitch);
protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;


public:
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCamera; }

	virtual void EquipWeapon(AWeaponBase* Weapon);
};
