#pragma once

#include "CoreMinimal.h"

class AActor;

namespace OutlierEnemyTargetRules
{
	enum class ETargetPriority : uint8
	{
		HackedTurret,
		PossessedDrone,
		Shooter,
		Partner,
		EnemyUnit,
		Invalid
	};

	OUTLIER_API ETargetPriority GetPriority(const AActor* TargetActor);
	OUTLIER_API bool IsUnavailable(const AActor* TargetActor);
}
