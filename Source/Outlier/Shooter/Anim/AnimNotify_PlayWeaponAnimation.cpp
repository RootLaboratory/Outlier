// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_PlayWeaponAnimation.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/WeaponBase.h"

void UAnimNotify_PlayWeaponAnimation::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference
)
{
	if (!MeshComp || !WeaponAnimation)
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

	USkeletalMeshComponent* WeaponMesh = Weapon->GetWeaponByView(bFirstPerson);
	if (!WeaponMesh)
	{
		return;
	}

	WeaponMesh->PlayAnimation(WeaponAnimation, bLooping);
}
