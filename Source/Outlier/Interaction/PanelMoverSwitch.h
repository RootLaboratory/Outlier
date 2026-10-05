#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableSwitchBase.h"
#include "PanelMoverSwitch.generated.h"

class APanelMover;

/** 판넬 전용 스위치. 상호작용하면 TargetPanel을 목표 위치까지 한 번 이동시킨다. */
UCLASS()
class OUTLIER_API APanelMoverSwitch : public AInteractableSwitchBase
{
	GENERATED_BODY()

protected:
	virtual bool ActivateTarget() override;

public:
	APanelMoverSwitch();

	UPROPERTY(EditInstanceOnly, Category = "Switch")
	TObjectPtr<APanelMover> TargetPanel;
};
