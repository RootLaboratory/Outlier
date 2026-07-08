#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "AutoTurret.generated.h"

UCLASS()
class OUTLIER_API AAutoTurret : public AEnemyBase
{
	GENERATED_BODY()

public:
	AAutoTurret();

	UFUNCTION(BlueprintPure, Category = "Enemy|Turret")
	bool HasCoreWeakPoint() const { return false; }

protected:
	virtual void ApplyClassStatOverrides() override;
	virtual void ApplyMovementFromRuntimeStat() override;
};
