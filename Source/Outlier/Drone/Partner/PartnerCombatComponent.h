// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerCombatComponent.generated.h"

class AWeaponBase;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerCombatComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerCombatComponent();

	virtual void BeginPlay() override;

	void TryStartAttack();
	void TryStopAttack();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Partner|Weapon")
	TSubclassOf<AWeaponBase> DefaultWeaponClass;

	UPROPERTY(EditDefaultsOnly, Category = "Partner|Weapon")
	uint8 bEquipDefaultWeaponOnBeginPlay : 1 = true;

	UFUNCTION(Server, Reliable)
	void ServerStartAttack();

	UFUNCTION(Server, Reliable)
	void ServerStopAttack();

private:
	void EquipDefaultWeapon_Server();
};
