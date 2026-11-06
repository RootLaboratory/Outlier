#include "Shooter/Anim/AnimNotify_MeleeRecoveryEnd.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"

void UAnimNotify_MeleeRecoveryEnd::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
	AMeleeWeaponBase* Weapon = Shooter ? Cast<AMeleeWeaponBase>(Shooter->GetCurrentWeapon()) : nullptr;
	if (Weapon && Weapon->IsCurrentAttackNotify(MeshComp, EventReference))
	{
		Shooter->HandleMeleeRecoveryEndNotify();
	}
}

// Branching Point도 큐 방식 Notify와 동일한 공격 수명 검증을 거친다.
void UAnimNotify_MeleeRecoveryEnd::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	Notify(Payload.SkelMeshComponent, Payload.SequenceAsset, Reference);
}
