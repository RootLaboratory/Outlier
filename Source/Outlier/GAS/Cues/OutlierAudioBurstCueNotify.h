#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Burst.h"
#include "GameplayTagContainer.h"
#include "OutlierAudioBurstCueNotify.generated.h"

class AActor;

/**
 * Burst cue notify that routes its sound through UOutlierAudioSubsystem.
 *
 * The parent's own Burst Sounds array plays via UGameplayStatics::PlaySoundAtLocation, which
 * bypasses the project's Bank/Context catalog and the SFX volume multiplier — sounds placed
 * there ignore the settings menu. Leave Burst Sounds empty on assets using this class.
 *
 * Everything else (particles, camera shake, decals, force feedback) still comes from the
 * parent's Burst arrays; only audio is redirected.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Outlier GCN Burst (Audio)"))
class OUTLIER_API UOutlierAudioBurstCueNotify : public UGameplayCueNotify_Burst
{
	GENERATED_BODY()

protected:
	virtual bool OnExecute_Implementation(
		AActor* Target,
		const FGameplayCueParameters& Parameters) const override;

	/** Audio Bank to route to. Audio.Type.Enemy / Player / Weapon / ... */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outlier Audio", meta = (Categories = "Audio.Type"))
	FGameplayTag AudioTypeTag;

	/** Selects the sound within that Bank. Leave unset to play nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outlier Audio", meta = (Categories = "Audio.Context"))
	FGameplayTag AudioContextTag;
};
