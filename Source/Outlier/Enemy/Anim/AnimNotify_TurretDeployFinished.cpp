#include "Enemy/Anim/AnimNotify_TurretDeployFinished.h"

#include "Enemy/AutoTurret.h"

void UAnimNotify_TurretDeployFinished::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AAutoTurret* Turret = MeshComp ? Cast<AAutoTurret>(MeshComp->GetOwner()) : nullptr;
	if (Turret && Turret->HasAuthority())
	{
		Turret->NotifyDeploySequenceFinished();
	}
}

FString UAnimNotify_TurretDeployFinished::GetNotifyName_Implementation() const
{
	return TEXT("Turret Deploy Finished");
}
