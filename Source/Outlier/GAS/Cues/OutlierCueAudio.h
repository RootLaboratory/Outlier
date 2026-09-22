#pragma once

#include "CoreMinimal.h"

class AActor;
struct FGameplayCueParameters;
struct FGameplayTag;

/**
 * Shared audio bridge for GameplayCue notifies.
 *
 * Kept free of the notify class hierarchy so Burst and Looping notifies can share one
 * implementation — the engine splits those into unrelated base classes
 * (UGameplayCueNotify_Static vs AGameplayCueNotify_Actor).
 */
namespace OutlierCueAudio
{
	/**
	 * Plays one sound through UOutlierAudioSubsystem for a cue that is already executing locally.
	 * No-op when either tag is unset, so a notify can opt out of audio by leaving them empty.
	 *
	 * Location is resolved with the same priority the engine uses for cue visuals
	 * (FGameplayCueNotify_PlacementInfo::FindSpawnTransform), so sound and particles match:
	 *   1. HitResult on the effect context   2. CueParameters.Location   3. the target actor
	 */
	OUTLIER_API bool Play(
		const AActor* Target,
		const FGameplayCueParameters& Parameters,
		const FGameplayTag& AudioTypeTag,
		const FGameplayTag& AudioContextTag);
}
