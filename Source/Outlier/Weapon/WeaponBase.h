// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Interface/InteractableInterface.h"
#include "Weapon/WeaponDataTypes.h"
#include "WeaponBase.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class USphereComponent;
class AWeaponSpawnPoint;
class AFirstPersonCharacter;
class UInteractableComponent;
class UProceduralAnimValues;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName WeaponName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	EWeaponType WeaponType = EWeaponType::Unarmed;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	EWeaponFireType FireType = EWeaponFireType::HitScan;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float AttackInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	float EffectiveRange = 1000.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	float MovementSpeedMultiplier = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float DamageFalloffStartRange = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float DamageFalloffMaxRange = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Range")
	float MinDamageMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = IK)
	FName LeftHandIKSocketName = FName("LeftHandIK");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = IK)
	FName LeftHandSprintIKSocketName = FName("LeftHandIK_Sprint");

	UPROPERTY(ReplicatedUsing = OnRep_EquippedState, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<ACharacter> WeaponOwner;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedState, VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsEquipped : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	uint8 bIsAttacking : 1 = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle WeaponCoreRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UDataTable> WeaponRangeTable;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FName RangeProfileId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle WeaponRangeRow;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	uint8 bWeaponDataInitialized : 1 = false;

	UPROPERTY()
	TObjectPtr<AWeaponSpawnPoint> OwningSpawnPoint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Pickup")
	float DropInstigatorPickupBlockDuration = 0.35f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AFirstPersonCharacter> DropPickupBlockedInteractor;

	UPROPERTY(Transient)
	float DropPickupBlockedUntilTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	TObjectPtr<UProceduralAnimValues> FirstPersonProceduralValues = nullptr;

protected:
	virtual void EnsureWeaponDataInitialized();
	virtual void InitializeFromDataTables();
	virtual void InitializeRangeFromDataTable();

	void SetEquippedCollisionEnabled(bool bEnabled);
	void SetPickupPresentation();
	void SetEquippedPresentation();
	void ApplyReplicatedPresentation();

	UFUNCTION()
	virtual void OnRep_EquippedState();

public:
	virtual void BeginPlay() override;

	virtual bool CanAttack() const;

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void StartAttack();

	virtual void StopAttack();

	virtual void PerformAttack();

	virtual void OnEquipped(ACharacter* NewOwner);

	virtual void AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner);
	void AttachWeaponMeshesToOwnerMeshes();
	virtual void ShowEquippedPresentation();

	virtual void OnUnequipped();

	virtual void OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy = nullptr);

	virtual UInteractableComponent* GetInteractableComponent() const override;

	virtual void Interact(class AFirstPersonCharacter* Interactor) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void OnOwnerLost();

	void SetOwningSpawnPoint(AWeaponSpawnPoint* SpawnPoint);

	EWeaponType GetWeaponType() const { return WeaponType; }
	EWeaponFireType GetFireType() const { return FireType; }
	EWeaponFireMode GetFireMode() const { return FireMode; }
	bool IsAttacking() const { return bIsAttacking; }
	bool IsEquipped() const { return bIsEquipped; }
	bool CanBePickedUpBy(const AFirstPersonCharacter* Interactor) const;
	float GetDamageAtDistance(float DistanceCm) const;
	float GetMovementSpeedMultiplier() const { return MovementSpeedMultiplier; }

	USkeletalMeshComponent* GetFirstPersonWeaponMesh() const { return FirstPersonWeaponMesh; }
	USkeletalMeshComponent* GetThirdPersonWeaponMesh() const { return ThirdPersonWeaponMesh; }

	UFUNCTION(BlueprintCallable, Category = IK)
	FName GetLeftHandIKSocketName() const
	{
		return LeftHandIKSocketName;
	}

	UFUNCTION(BlueprintCallable, Category = IK)
	FName GetLeftHandSprintIKSocketName() const
	{
		return LeftHandSprintIKSocketName;
	}

	UFUNCTION(BlueprintCallable, Category = IK)
	USkeletalMeshComponent* GetWeaponByView(bool bFirstPerson) const
	{
		return bFirstPerson ? FirstPersonWeaponMesh : ThirdPersonWeaponMesh;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Animation")
	const UProceduralAnimValues* GetFirstPersonProceduralValues() const
	{
		return FirstPersonProceduralValues;
	}
};
