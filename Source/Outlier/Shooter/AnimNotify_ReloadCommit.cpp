// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/AnimNotify_ReloadCommit.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"

void UAnimNotify_ReloadCommit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		return;
	}

	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Shooter)
	{
		return;
	}

	Shooter->HandleReloadCommitNotify();
}
