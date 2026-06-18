// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_ReloadEnd.h"
#include "Shooter/ShooterCharacter.h"

void UAnimNotify_ReloadEnd::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
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

	Shooter->FinishReloadInternal();
}
