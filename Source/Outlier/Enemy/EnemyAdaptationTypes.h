#pragma once

#include "CoreMinimal.h"
#include "EnemyAdaptationTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyAdaptationStage : uint8
{
	Normal,
	ShieldPreview,
	ResistanceLevel1,
	ResistanceLevel2,
	ResistanceMax
};

UENUM(BlueprintType)
enum class EEnemyFinalKillCategory : uint8
{
	Gun,
	NonGun,
	Ignore
};

