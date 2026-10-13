#pragma once

#include "CoreMinimal.h"
#include "Tutorial/Trigger/Tuto_EventTriggerInterface.h"
#include "Weapon/RangedWeaponBase.h"
#include "Tuto_Pistol.generated.h"

class UOutlierWorldGuideTargetComponent;

UCLASS()
class OUTLIER_API ATuto_Pistol : public ARangedWeaponBase, public ITuto_EventTriggerInterface
{
	GENERATED_BODY()

public:
	ATuto_Pistol();

	/** Configure WidgetID, WidgetClass and the marker anchor on this inherited component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|World Guide")
	TObjectPtr<UOutlierWorldGuideTargetComponent> WorldGuideTarget;
	virtual bool Interact(AFirstPersonCharacter* Interactor) override;
	virtual FGameplayTag GetEventTag(int32 EventIndex) const override
	{
		return EventTriggers.IsValidIndex(EventIndex) ? EventTriggers[EventIndex].EventTag : FGameplayTag();
	}

protected:
	/** Slot 0: successful pickup. Additional situations can select other slots in C++ subclasses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Event", meta = (TitleProperty = "EventTag"))
	TArray<FTuto_EventTriggerData> EventTriggers;

	virtual void HandleWeaponCollected(AOutlierPlayerState* SourcePlayerState);
};
