#include "Shooter/Anim/AnimNotifyState_MeleeTrace.h"

#include "Shooter/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	AShooterCharacter* ResolveShooter(const USkeletalMeshComponent* MeshComp)
	{
		return MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;
	}
}

void UAnimNotifyState_MeleeTrace::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp))
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

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp))
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

	if (AShooterCharacter* Shooter = ResolveShooter(MeshComp))
	{
		Shooter->HandleMeleeTraceEndNotify();
	}
}
