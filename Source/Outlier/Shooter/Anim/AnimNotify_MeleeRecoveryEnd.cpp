#include "Shooter/Anim/AnimNotify_MeleeRecoveryEnd.h"
#include "Shooter/ShooterCharacter.h"

void UAnimNotify_MeleeRecoveryEnd::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		Shooter->HandleMeleeRecoveryEndNotify();
	}
}
