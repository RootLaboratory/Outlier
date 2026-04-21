// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shooter/ShooterCharacterComponentBase.h"
#include "Shooter/ShooterCharacter.h"

AShooterCharacter* UShooterCharacterComponentBase::GetShooterCharacter() const
{
	return Cast<AShooterCharacter>(GetOwner());
}
