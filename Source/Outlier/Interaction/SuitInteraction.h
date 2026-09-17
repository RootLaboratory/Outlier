#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "SuitInteraction.generated.h"

class AFirstPersonCharacter;
class ARangedWeaponBase;
class AShooterCharacter;
class AWeaponBase;
class UInteractableComponent;
class USceneComponent;
class USkeletalMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class OUTLIER_API ASuitInteraction : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	ASuitInteraction();

	virtual UInteractableComponent* GetInteractableComponent() const override;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> SuitDisplayMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Mesh")
	TObjectPtr<USkeletalMesh> ShooterFirstPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Mesh")
	TObjectPtr<USkeletalMesh> ShooterThirdPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Weapon")
	TSubclassOf<AWeaponBase> ShooterRifleClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Weapon")
	TSubclassOf<ARangedWeaponBase> PartnerWeaponClass;

private:
	bool SpawnStoredWeapons();
	AWeaponBase* SpawnStoredWeapon(UClass* WeaponClass);
	bool ApplySuit(AShooterCharacter* ShooterCharacter);
	void ConsumeInteraction();
	void DestroyAfterInteraction();
	void DestroyStoredWeapons();

	UPROPERTY(Transient)
	TObjectPtr<AWeaponBase> StoredShooterRifle;

	UPROPERTY(Transient)
	TObjectPtr<ARangedWeaponBase> StoredPartnerWeapon;

	bool bConsumed = false;
};
