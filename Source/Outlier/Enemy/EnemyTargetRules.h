#pragma once

#include "CoreMinimal.h"

class AActor;

namespace OutlierEnemyTargetRules
{
	OUTLIER_API bool IsUnavailable(const AActor* TargetActor);
}
