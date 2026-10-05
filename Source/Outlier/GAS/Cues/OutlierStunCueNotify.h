#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Looping.h"
#include "OutlierStunCueNotify.generated.h"

/**
 * Shared presentation for EMP and weapon stuns.
 * Create a Blueprint child in /Game/GameplayCues and assign the desired Niagara
 * system in Looping Effects -> Looping Particles. The Stun GE owns its lifetime.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Outlier Stun Looping Cue"))
class OUTLIER_API AOutlierStunCueNotify : public AGameplayCueNotify_Looping
{
	GENERATED_BODY()

public:
	AOutlierStunCueNotify();

protected:
	/** Keep the shared cue registered even when the Blueprint is named GC_Stun. */
	virtual void DeriveGameplayCueTagFromAssetName() override;
};
