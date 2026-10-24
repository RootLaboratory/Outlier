#include "GAS/Cues/OutlierAudioBurstCueNotify.h"
#include "Audio/OutlierAudioFeedback.h"

#include "GAS/Cues/OutlierCueAudio.h"
#include "GameFramework/Actor.h"
#include "Enemy/EnemyBase.h"
#include "GameplayTags/OutlierGameplayTags.h"

bool UOutlierAudioBurstCueNotify::OnExecute_Implementation(
	AActor* Target,
	const FGameplayCueParameters& Parameters) const
{
	const FOutlierSourceTagBurstEffects* MatchingEffects = SourceTagBurstEffects.FindByPredicate(
		[&Parameters](const FOutlierSourceTagBurstEffects& Entry)
		{
			return Entry.SourceTag.IsValid() && Parameters.AggregatedSourceTags.HasTag(Entry.SourceTag);
		});

	bool bResult = false;
	if (MatchingEffects)
	{
		FGameplayCueNotify_SpawnContext SpawnContext(Target ? Target->GetWorld() : GetWorld(), Target, Parameters);
		SpawnContext.SetDefaultSpawnCondition(&DefaultSpawnCondition);
		SpawnContext.SetDefaultPlacementInfo(&DefaultPlacementInfo);
		if (DefaultSpawnCondition.ShouldSpawn(SpawnContext))
		{
			FGameplayCueNotify_SpawnResult SpawnResult;
			MatchingEffects->BurstEffects.ExecuteEffects(SpawnContext, SpawnResult);
			OnBurst(Target, Parameters, SpawnResult);
		}
	}
	else
	{
		bResult = Super::OnExecute_Implementation(Target, Parameters);
	}

	FGameplayTag Context = AudioContextTag;
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(Target))
	{
		Context = FGameplayTag();
		for (FGameplayTag Tag : Parameters.AggregatedTargetTags)
			if (Tag.MatchesTag(OutlierAudioFeedback::Tag(TEXT("Audio.Context")))) { Context = Tag; break; }
		if (GameplayCueTag == OutlierGameplayTags::Cue::Drone::Death()) OutlierAudioFeedback::StopLoops(Enemy);
	}
	OutlierCueAudio::Play(Target, Parameters, AudioTypeTag, Context);

	return bResult;
}
