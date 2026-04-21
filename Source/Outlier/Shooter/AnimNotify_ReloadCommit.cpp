// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/AnimNotify_ReloadCommit.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "OutlierNetUtils.h"

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

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s AnimNotify_ReloadCommit Mesh=%s Animation=%s"),
		OutlierNet::GetNetPrefix(Shooter),
		*Shooter->GetName(),
		*GetNameSafe(MeshComp),
		*GetNameSafe(Animation)
	);

	Shooter->HandleReloadCommitNotify();
}
