#include "Shooter/Anim/AnimNotify_MeleeHit.h"
#include "Shooter/ShooterCharacter.h"

void UAnimNotify_MeleeHit::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		Shooter->HandleMeleeHitNotify();
	}
}
