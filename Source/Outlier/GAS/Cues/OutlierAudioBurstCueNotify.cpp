#include "GAS/Cues/OutlierAudioBurstCueNotify.h"

#include "GAS/Cues/OutlierCueAudio.h"
#include "GameFramework/Actor.h"

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

	OutlierCueAudio::Play(Target, Parameters, AudioTypeTag, AudioContextTag);

	return bResult;
}
