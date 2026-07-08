#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "VECDrone.generated.h"

UCLASS()
class OUTLIER_API AVECDrone : public AEnemyBase
{
	GENERATED_BODY()

public:
	AVECDrone();

protected:
	virtual void ApplyMovementFromRuntimeStat() override;
};
