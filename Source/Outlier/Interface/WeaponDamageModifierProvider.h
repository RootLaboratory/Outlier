#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WeaponDamageModifierProvider.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FWeaponDamageModifiers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	float StunMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	float DamageMultiplier = 1.0f;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWeaponDamageModifierProvider : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IWeaponDamageModifierProvider
{
	GENERATED_BODY()
public:
	virtual void GetWeaponDamageModifiers(FWeaponDamageModifiers& OutModifiers) const = 0;
};
