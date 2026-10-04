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
	virtual bool DefersInteractionCompletion() const override { return true; }
	bool CanReserveFor(AShooterCharacter* ShooterCharacter) const;
	bool IsReservedFor(const AShooterCharacter* ShooterCharacter) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UStaticMeshComponent> SuitDisplayMesh;

	// Shooter BP의 SuitPresentation 미설정 기간에만 사용하는 이전 콘텐츠 호환 필드다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Mesh")
	TObjectPtr<USkeletalMesh> ShooterFirstPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Mesh")
	TObjectPtr<USkeletalMesh> ShooterThirdPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Weapon")
	TSubclassOf<AWeaponBase> ShooterRifleClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Suit|Weapon")
	TSubclassOf<ARangedWeaponBase> PartnerWeaponClass;

private:
	friend class AShooterCharacter;
	bool ReserveFor(AShooterCharacter* ShooterCharacter);
	bool CommitReservedSuit(AShooterCharacter* ShooterCharacter);
	void ReleaseReservation(AShooterCharacter* ShooterCharacter);
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
	TWeakObjectPtr<AShooterCharacter> ReservedShooter;
};
