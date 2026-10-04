#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Burst.h"
#include "GameplayTagContainer.h"
#include "OutlierAudioBurstCueNotify.generated.h"

class AActor;

/** A burst preset selected using the cue's aggregated source tags. */
USTRUCT(BlueprintType)
struct FOutlierSourceTagBurstEffects
{
	GENERATED_BODY()

	/** Matches this tag or one of its children in AggregatedSourceTags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCueNotify")
	FGameplayTag SourceTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCueNotify")
	FGameplayCueNotify_BurstEffects BurstEffects;
};

/**
 * Burst cue notify that routes its sound through UOutlierAudioSubsystem.
 *
 * The parent's own Burst Sounds array plays via UGameplayStatics::PlaySoundAtLocation, which
 * bypasses the project's Bank/Context catalog and the SFX volume multiplier — sounds placed
 * there ignore the settings menu. Leave Burst Sounds empty on assets using this class.
 *
 * Source Tag Burst Effects selects the first matching preset without a Blueprint graph.
 * If none matches, the parent's Burst Effects remains the fallback.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Outlier GCN Burst (Audio)"))
class OUTLIER_API UOutlierAudioBurstCueNotify : public UGameplayCueNotify_Burst
{
	GENERATED_BODY()

protected:
	virtual bool OnExecute_Implementation(
		AActor* Target,
		const FGameplayCueParameters& Parameters) const override;

	/** First matching entry replaces the default Burst Effects. Leave Burst Sounds empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GCN Effects", meta = (TitleProperty = "SourceTag"))
	TArray<FOutlierSourceTagBurstEffects> SourceTagBurstEffects;

	/** Audio Bank to route to. Audio.Type.Enemy / Player / Weapon / ... */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outlier Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag AudioTypeTag;

	/** Selects the sound within that Bank. Leave unset to play nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outlier Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag AudioContextTag;
};
