// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShooterCharacterComponentBase.generated.h"

class AShooterCharacter;

UCLASS(Abstract)
class OUTLIER_API UShooterCharacterComponentBase : public UActorComponent
{
	GENERATED_BODY()

protected:
	AShooterCharacter* GetShooterCharacter() const;
};
