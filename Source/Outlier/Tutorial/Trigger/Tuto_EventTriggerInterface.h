#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Trigger/Tuto_EventTriggerData.h"
#include "UObject/Interface.h"
#include "Tuto_EventTriggerInterface.generated.h"

class AOutlierPlayerState;
class UWorld;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTuto_EventTriggerInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	/**
	 * Server confirms one event and sends the same ID to both linked players.
	 * Each actor's slot is submitted once for the actor's lifetime, independently of PairID.
	 * Client DT/UI acceptance happens separately. Empty entries and repeated slots return false.
	 */
	virtual bool TriggerEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState);
	/** Normal completion advances the matching region's chain; defaults to Upper and GIF, leaving voice playing. */
	virtual bool CompleteEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	/** Removes active/pending matching requests without advancing their chains. */
	virtual bool CancelEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	/** Completes only matching requests whose DT end condition is Overlap. */
	virtual bool NotifyEventOverlap(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventRegion Region = ETuto_EventRegion::Both);
	virtual FGameplayTag GetEventTag(int32 EventIndex) const = 0;

	/** Server transport shared by actor triggers and the editor/console request route. */
	static bool DispatchEventTag(UWorld* World, FGameplayTag EventTag, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventOperation Operation, ETuto_EventRegion Region, const FString& Source);

private:
	bool DispatchEvent(int32 EventIndex, AOutlierPlayerState* SourcePlayerState,
		ETuto_EventOperation Operation, ETuto_EventRegion Region);
	TSet<int32> SubmittedEventIndices;
	bool bDispatchingEvent = false;
};
