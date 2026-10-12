#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "OutlierEventTriggerVolume.generated.h"

class UBoxComponent;

UENUM()
enum class EOutlierEventTriggerRole : uint8
{
	Shooter,
	Partner,
	/** Any FirstPersonCharacter. */
	Both
};

/**
 * Trigger only: sends EventTag for the locally controlled pawn of the target role.
 * Presentation and lifetime come from the event data, so every machine handles its own pawn without RPCs.
 * Console: Outlier.Event.Send <EventTag> takes the same path without a volume.
 */
UCLASS()
class OUTLIER_API AOutlierEventTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	AOutlierEventTriggerVolume();

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(EditInstanceOnly, Category = "Event", meta = (Categories = "Event"))
	FGameplayTag EventTag;

	UPROPERTY(EditInstanceOnly, Category = "Event")
	EOutlierEventTriggerRole TargetRole = EOutlierEventTriggerRole::Both;

	/** Once per local player, after the event is accepted. */
	UPROPERTY(EditInstanceOnly, Category = "Event")
	bool bTriggerOnce = true;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool IsTargetPawn(const AActor* Actor) const;

	bool bTriggered = false;
};
