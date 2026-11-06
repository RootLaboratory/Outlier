#include "Shooter/Anim/AnimNotifyState_MeleeTrace.h"

#include "Shooter/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyEndDataContext.h"
#include "Animation/AnimNotifyQueue.h"

namespace
{
	AShooterCharacter* ResolveShooter(const USkeletalMeshComponent* MeshComp, const FAnimNotifyEventReference& EventReference)
	{
		AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
		AMeleeWeaponBase* Weapon = Shooter ? Cast<AMeleeWeaponBase>(Shooter->GetCurrentWeapon()) : nullptr;
		return Weapon && Weapon->IsCurrentAttackNotify(MeshComp, EventReference) ? Shooter : nullptr;
	}
}

void UAnimNotifyState_MeleeTrace::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp, EventReference))
	{
		Shooter->HandleMeleeTraceBeginNotify();
	}
}

void UAnimNotifyState_MeleeTrace::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp, EventReference))
	{
		Shooter->HandleMeleeTraceTickNotify();
	}
}

void UAnimNotifyState_MeleeTrace::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp, EventReference))
	{
		Shooter->HandleMeleeTraceEndNotify();
	}
}

// Branching Point의 기본 구현은 몽타주 문맥을 버리므로 payload의 인스턴스 ID를 유지한다.
void UAnimNotifyState_MeleeTrace::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	NotifyBegin(Payload.SkelMeshComponent, Payload.SequenceAsset, Payload.NotifyEvent ? Payload.NotifyEvent->GetDuration() : 0.0f, Reference);
}

void UAnimNotifyState_MeleeTrace::BranchingPointNotifyTick(FBranchingPointNotifyPayload& Payload, float FrameDeltaTime)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	NotifyTick(Payload.SkelMeshComponent, Payload.SequenceAsset, FrameDeltaTime, Reference);
}

void UAnimNotifyState_MeleeTrace::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	if (Payload.bReachedEnd)
	{
		Reference.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
	}
	NotifyEnd(Payload.SkelMeshComponent, Payload.SequenceAsset, Reference);
}
