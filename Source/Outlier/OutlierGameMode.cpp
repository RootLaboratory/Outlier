// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Shooter/ShooterCharacter.h"

AOutlierGameMode::AOutlierGameMode()
{
	// stub
}

void AOutlierGameMode::Logout(AController* Exiting)
{
	if (Exiting)
	{
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(Exiting->GetPawn()))
		{
			ShooterCharacter->CleanupOwnedWeapons();
		}
	}

	Super::Logout(Exiting);
}
