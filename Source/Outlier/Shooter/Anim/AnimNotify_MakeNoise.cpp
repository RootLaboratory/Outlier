// Fill out your copyright notice in the Description page of Project Settings.


#include "Shooter/Anim/AnimNotify_MakeNoise.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Team/OutlierTeamIds.h"
#include "Shooter/ShooterCharacter.h"
#include "Perception/AISense_Hearing.h"
#include "Shooter/Anim/FootstepNoiseTestProbe.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Outlier.h"

#if WITH_DEV_AUTOMATION_TESTS
OutlierFootstepTesting::FReported& OutlierFootstepTesting::Reported()
{
	static FReported Delegate;
	return Delegate;
}
#endif

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
	static const IConsoleVariable* Diagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Enemy.PerceptionDiagnostics"));
	if (Diagnostics && Diagnostics->GetInt() != 0)
	{
		UE_LOG(LogOutlier, Log,
			TEXT("[EnemyHearingDiag] Stage=Generate Source=%s OccurredAt=%.3f Location=%s Tag=%s Loudness=%.2f MaxRange=%.1f"),
			*Source->GetName(), MeshComp->GetWorld()->GetTimeSeconds(), *Source->GetActorLocation().ToCompactString(),
			*NoiseTag.ToString(), Loudness, MaxRange);
	}
	UAISense_Hearing::ReportNoiseEvent(
		MeshComp->GetWorld(),
		Source->GetActorLocation(),
		Loudness,
		Source,
		MaxRange,
		NoiseTag
	);
#if WITH_DEV_AUTOMATION_TESTS
	OutlierFootstepTesting::Reported().Broadcast(MeshComp, Source->GetActorLocation());
#endif
}
