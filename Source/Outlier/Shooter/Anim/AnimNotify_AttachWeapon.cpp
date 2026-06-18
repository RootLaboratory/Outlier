// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_AttachWeapon.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"

void UAnimNotify_AttachWeapon::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp || !Animation)
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Shooter)
	{
		return;
	}

	AWeaponBase* Weapon = Cast<AWeaponBase>(Shooter->GetCurrentWeapon());
	if (!Weapon)
	{
		return;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[AnimNotify_Attach] Mesh=%s Owner=%s Anim=%s"),
		*GetNameSafe(MeshComp),
		*GetNameSafe(MeshComp ? MeshComp->GetOwner() : nullptr),
		*GetNameSafe(Animation)
	);

	Weapon->AttachWeaponMeshesToOwnerMeshes();
	Weapon->ShowEquippedPresentation();
}
