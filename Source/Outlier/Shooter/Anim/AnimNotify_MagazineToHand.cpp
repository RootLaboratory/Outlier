// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_MagazineToHand.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"

void UAnimNotify_MagazineToHand::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if(!MeshComp)
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Shooter)
	{
		return;
	}

	ARangedWeaponBase* Weapon = Cast<ARangedWeaponBase>(Shooter->GetCurrentWeapon());
	if (!Weapon)
	{
		return;
	}

	Weapon->AttachMagazineToLeftHand(Shooter);
}
