#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MeleeIgnoreInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMeleeIgnoreInterface : public UInterface
{
	GENERATED_BODY()
};

// Marks non-physical actors that melee traces must exclude from hits and surface feedback.
class OUTLIER_API IMeleeIgnoreInterface
{
	GENERATED_BODY()
};
