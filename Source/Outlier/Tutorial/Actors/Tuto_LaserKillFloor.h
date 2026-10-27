#pragma once

#include "CoreMinimal.h"
#include "Environment/LaserKillFloor.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"
#include "Tuto_LaserKillFloor.generated.h"

/** 실제 위험 판정이 꺼진 뒤 스위치에 완료를 알린다. 체크포인트 확인을 위해 액터는 유지한다. */
UCLASS()
class OUTLIER_API ATuto_LaserKillFloor : public ALaserKillFloor, public ITuto_ProgressSourceInterface
{
	GENERATED_BODY()
public:
	ATuto_LaserKillFloor();
	virtual void SetHazardEnabled(bool bEnabled) override;
	virtual bool IsProgressCompleted() const override { return !IsHazardEnabled(); }
};
