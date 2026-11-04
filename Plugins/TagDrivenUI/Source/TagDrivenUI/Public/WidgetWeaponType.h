#pragma once

#include "CoreMinimal.h"
#include "WidgetWeaponType.generated.h"

UENUM(BlueprintType)
enum class EWidgetWeaponType : uint8
{
	Unarmed,
	Pistol,
	Rifle,
	Melee
};
