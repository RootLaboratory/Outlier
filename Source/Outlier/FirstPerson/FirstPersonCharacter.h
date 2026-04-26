// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Weapon/WeaponBase.h"
#include "FirstPersonCharacter.generated.h"

class USkeletalMeshComponent;
class UCameraComponent;
class USceneComponent;
class UInputAction;
struct FInputActionValue;

UCLASS()
class OUTLIER_API AFirstPersonCharacter : public ACharacter
{
	GENERATED_BODY()

protected:
	/** Pawn Mesh : first person view(arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First Person Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCamera;

	/** First Person Camera Root */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USceneComponent* FirstPersonCameraRoot;

	/** Root used to keep first-person arms and weapon in camera space */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	USceneComponent* FirstPersonViewModelRoot;


	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* LookAction;

	/** Attack Weapon Input Action */
	UPROPERTY(EditAnywhere, Category = Input)
	UInputAction* AttackAction;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, EditAnywhere, Category = Weapon)
	AWeaponBase* CurrentWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(Transient)
	TObjectPtr<AWeaponBase> LastReplicatedWeapon;

	UFUNCTION()
	void OnRep_CurrentWeapon();

public:
	/** Sets default values for this character's properties */
	AFirstPersonCharacter();

protected:

	virtual void TryStartAttack();

	virtual void TryStopAttack();

	void MoveInput(const FInputActionValue& Value);

	void LookInput(const FInputActionValue& Value);

	void DoMove(float Right, float Forward);

	void DoAim(float Yaw, float Pitch);
protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void BeginPlay() override;

public:
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCamera; }

	USceneComponent* GetFirstPersonCameraRoot() const { return FirstPersonCameraRoot; }

	USceneComponent* GetFirstPersonViewModelRoot() const { return FirstPersonViewModelRoot; }

	virtual void EquipWeapon(AWeaponBase* Weapon);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	EWeaponType GetWeaponType() const;

	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }
};
