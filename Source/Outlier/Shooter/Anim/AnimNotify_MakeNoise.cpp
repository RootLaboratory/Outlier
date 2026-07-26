// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_MakeNoise.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Perception/AISense_Hearing.h"

void UAnimNotify_MakeNoise::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}


	AShooterCharacter* Shooter = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!IsValid(Shooter) || !Shooter->HasAuthority())
	{
		return;
	}

	const AOutlierPlayerState* PlayerState = Shooter->GetPlayerState<AOutlierPlayerState>();
	const FGameplayTag CurrentRoomTag = Shooter->GetCurrentRoomTag();
	if (PlayerState && CurrentRoomTag.IsValid())
	{
		if (const UEnemyRoomSubsystem* RoomSubsystem = MeshComp->GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			if (RoomSubsystem->IsRoomInCombat(PlayerState->GetArenaId(), CurrentRoomTag))
			{
				return;
			}
		}
	}

	UAISense_Hearing::ReportNoiseEvent(
		MeshComp->GetWorld(),
		Shooter->GetActorLocation(),
		Loudness,
		Shooter,
		MaxRange,
		NoiseTag
	);
}
