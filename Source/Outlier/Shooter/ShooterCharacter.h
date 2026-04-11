// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "ShooterCharacter.generated.h"

class UInputAction;
struct FInputActionValue;
class UShooterInputConfig;
class AWeaponBase;

/**
 * 
 */
UCLASS(abstract)
class OUTLIER_API AShooterCharacter : public AFirstPersonCharacter
{
	GENERATED_BODY()

protected:
	/** Input Config */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Input)
	TObjectPtr<UShooterInputConfig> InputConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<TObjectPtr<AWeaponBase>> OwnedWeapons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Health)
	float MaxHP = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_CurHP, EditAnywhere, BlueprintReadWrite, Category = Health)
	float CurHP = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	float CurrentLeanValue = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = Movement)
	float WalkSpeed = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = Movement)
	float SprintSpeed = 600.0f;

	UPROPERTY(EditDefaultsOnly, Category = Status)
	float InteractRange = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsAiming : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSliding : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	uint8 bIsSuitMenuOpen : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Status)
	uint8 bIsDead : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Combat)
	int32 SelectedSuitIndex = 0; // 이후에 Suit 관련 만들면서 거기에 있는 enum 값으로 교체?


public:

	/** Name of the first person mesh weapon socket */
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Constructor */
	AShooterCharacter();

protected:
	virtual void BeginPlay() override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	virtual void TryStartAttack() override;

	virtual void TryStopAttack() override;

	void TryReload();

	void TrySwitchWeapon1();

	void TrySwitchWeapon2();

	void TrySwitchWeapon3();

	void SelectWeaponByIndex(int32 SlotIndex);

	void TryStartAim();

	void TryStopAim();

	void TryStartSprint();

	void TryStopSprint();

	void TryStartCrouchOrSlide();

	void TryStopCrouch();

	void TryInteract();

	void TryOpenSuitMenu();

	void TryCloseSuitMenu();

	void UpdateSuitSelection(const FInputActionValue& Value);

	void TryUseSuit();

	void TrySlide();

	void TryLean(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* TargetActor);

	UFUNCTION(Server, Reliable)
	void ServerStartAttack();

	UFUNCTION(Server, Reliable)
	void ServerStopAttack();

	UFUNCTION(Server, Reliable)
	void ServerReload();

	void Die();

public:

	UFUNCTION()
	void OnRep_CurHP();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void ApplyDamageInternal(float DamageAmount);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();
};
