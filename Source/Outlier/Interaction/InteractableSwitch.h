#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableSwitchBase.h"
#include "InteractableSwitch.generated.h"

class AInteractableDoor;

/**
 * 문 전용 스위치. 해킹 여부는 BP 태그로 나뉜다(BP_DoorSwitch: 일반, BP_LevelDoorSwitch: 해킹 잠금).
 * 클래스/프로퍼티 이름은 레벨에 배치된 TargetDoor 참조를 유지하려고 그대로 둔다.
 */
UCLASS()
class OUTLIER_API AInteractableSwitch : public AInteractableSwitchBase
{
	GENERATED_BODY()

protected:
	virtual bool ActivateTarget() override;

public:
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<AInteractableDoor> TargetDoor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bCanToggleDoor = true;
};
