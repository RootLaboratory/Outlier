// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shooter/ShooterCharacterComponentBase.h"
#include "ShooterHealthComponent.generated.h"

UCLASS(ClassGroup=(Shooter), meta=(BlueprintSpawnableComponent))
class OUTLIER_API UShooterHealthComponent : public UShooterCharacterComponentBase
{
	GENERATED_BODY()

public:
	UShooterHealthComponent();

	void ApplyDamage(float DamageAmount);
	void Die();
};
