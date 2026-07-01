// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotifyState_Slide.h"
#include "Shooter/ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_Slide::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;

	if (!Shooter)
	{
		return;
	}

	Shooter->BeginSlideCameraEffect(CameraRollDegrees, TotalDuration);
}

void UAnimNotifyState_Slide::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	AShooterCharacter* Shooter = MeshComp ? Cast<AShooterCharacter>(MeshComp->GetOwner()) : nullptr;

	if (!Shooter)
	{
		return;
	}

	Shooter->EndSlideCameraEffect();
}
