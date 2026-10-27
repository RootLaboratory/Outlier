#pragma once

#include "CoreMinimal.h"
#include "Interaction/PanelMover.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"
#include "Tuto_PanelMover.generated.h"

/** 이동 요청 수락이 아니라 실제 도착 시 연결된 스위치에 완료를 알린다. */
UCLASS()
class OUTLIER_API ATuto_PanelMover : public APanelMover, public ITuto_ProgressSourceInterface
{
	GENERATED_BODY()
public:
	virtual bool IsProgressCompleted() const override { return bMoveCompleted; }
protected:
	virtual void FinishMove() override;
private:
	bool bMoveCompleted = false;
};
