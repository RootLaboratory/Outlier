#pragma once

#include "CoreMinimal.h"
#include "Interaction/PanelMover.h"
#include "Tutorial/Trigger/Tuto_ProgressSourceInterface.h"
#include "Tuto_PanelMover.generated.h"

/** 실제 도착 시 패널 자체의 완료를 알린다. 스위치 진행은 상호작용 성공 시 별도로 완료한다. */
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
