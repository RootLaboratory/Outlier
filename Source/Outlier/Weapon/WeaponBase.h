// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponBase.generated.h"

class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Ranged,
	Melee
};

UCLASS(Abstract)
class OUTLIER_API AWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AWeaponBase();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Weapon)
	USkeletalMeshComponent* Mesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName WeaponName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	EWeaponType WeaponType = EWeaponType::Ranged;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float AttackInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float EffectiveRange = 1000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<ACharacter> WeaponOwner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsEquipped : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsAttacking : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bAttackOnCooldown : 1 = false;

public:	
	virtual bool CanAttack() const;

	virtual void StartAttack();

	virtual void StopAttack();

	virtual void PerformAttack();

	virtual void OnEquipped(ACharacter* NewOwner);

	virtual void OnUnequipped();
};
