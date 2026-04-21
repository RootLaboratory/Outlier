// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "WeaponBase.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Unarmed,
	Pistol,
	Rifle,
	Melee
};

UCLASS(Abstract)
class OUTLIER_API AWeaponBase : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AWeaponBase();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USkeletalMeshComponent> ThirdPersonWeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<USphereComponent> InteractionCollision;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName WeaponName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	EWeaponType WeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float AttackInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float EffectiveRange = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = IK)
	FName LeftHandIKSocketName = FName("LeftHandIK");

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<ACharacter> WeaponOwner;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsEquipped : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsAttacking : 1 = false;

protected:
	void SetEquippedCollisionEnabled(bool bEnabled);
	void SetPickupPresentation();
	void SetEquippedPresentation();

public:	
	virtual bool CanAttack() const;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void StartAttack();

	virtual void StopAttack();

	virtual void PerformAttack();

	virtual void OnEquipped(ACharacter* NewOwner);

	virtual void OnUnequipped();

	virtual void OnDropped(const FTransform& DropTransform);

	virtual void Interact(class AFirstPersonCharacter* Interactor) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	EWeaponType GetWeaponType() const { return WeaponType; }

	USkeletalMeshComponent* GetFirstPersonWeaponMesh() const { return FirstPersonWeaponMesh; }
	USkeletalMeshComponent* GetThirdPersonWeaponMesh() const { return ThirdPersonWeaponMesh; }

	UFUNCTION(BlueprintCallable, Category = IK)
	FName GetLeftHandIKSocketName() const { return LeftHandIKSocketName; }

	UFUNCTION(BlueprintCallable, Category = IK)
	USkeletalMeshComponent* GetWeaponByView(bool bFirstPerson) const
	{
		return bFirstPerson ? FirstPersonWeaponMesh : ThirdPersonWeaponMesh;
	}
};
