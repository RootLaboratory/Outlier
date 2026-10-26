#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractableSwitchBase.h"
#include "LaserControlSwitch.generated.h"

class ALaserKillFloor;

/**
 * 레이저 제어 스위치. 처음엔 State.Locked로 잠겨 있고, 해킹(Hack.Effect.Unblock) 성공 시 잠금이 풀린다.
 * 잠금이 풀린 뒤 상호작용하면 TargetLaserFloors를 모두 파괴한다.
 * 진행 저장은 SwitchId(ActivatedSwitch)를 쓰며, 체크포인트 리로드 후 BeginPlay에서 다시 파괴해 상태를 맞춘다.
 */
UCLASS()
class OUTLIER_API ALaserControlSwitch : public AInteractableSwitchBase
{
	GENERATED_BODY()

public:
	ALaserControlSwitch();

protected:
	virtual void BeginPlay() override;
	virtual bool ActivateTarget() override;

public:
	UPROPERTY(EditInstanceOnly, Category = "Switch")
	TArray<TObjectPtr<ALaserKillFloor>> TargetLaserFloors;

private:
	/** 서버 전용. 하나라도 파괴했으면 true. */
	bool DestroyLaserFloors();
};
