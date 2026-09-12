#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/SuitInteraction.h"
#include "Weapon/RangedWeaponBase.h"
#include "SuitInteractionTestActors.generated.h"

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestRifle : public ARangedWeaponBase
{
	GENERATED_BODY()

public:
	ASuitInteractionTestRifle()
	{
		WeaponType = EWeaponType::Rifle;
	}
};

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestPartnerWeapon : public ARangedWeaponBase
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestActor : public ASuitInteraction
{
	GENERATED_BODY()

public:
	void Configure(
		UStaticMesh* DisplayMesh,
		USkeletalMesh* FirstPersonMesh,
		USkeletalMesh* ThirdPersonMesh)
	{
		SuitDisplayMesh->SetStaticMesh(DisplayMesh);
		ShooterFirstPersonMesh = FirstPersonMesh;
		ShooterThirdPersonMesh = ThirdPersonMesh;
		ShooterRifleClass = ASuitInteractionTestRifle::StaticClass();
		PartnerWeaponClass = ASuitInteractionTestPartnerWeapon::StaticClass();
	}
};
