// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_MakeNoise.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Team/OutlierTeamIds.h"
#include "Shooter/ShooterCharacter.h"
#include "Perception/AISense_Hearing.h"

void UAnimNotify_MakeNoise::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}


	AActor* Source = MeshComp->GetOwner();
	if (!IsValid(Source) || !Source->HasAuthority())
	{
		return;
	}

	const AEnemyBase* EnemySource = Cast<AEnemyBase>(Source);
	if (!Source->IsA<AShooterCharacter>() && !Source->IsA<APartnerCharacter>()
		&& (!EnemySource || EnemySource->GetGenericTeamId() != FGenericTeamId(OutlierTeamIds::Player)))
	{
		return;
	}

	// 전투 중에도 보고한다. 조사에 사용할지는 실제로 들은 Enemy가 판단한다.
	UAISense_Hearing::ReportNoiseEvent(
		MeshComp->GetWorld(),
		Source->GetActorLocation(),
		Loudness,
		Source,
		MaxRange,
		NoiseTag
	);
}
