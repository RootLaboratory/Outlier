// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/OutlierArenaPausePlayerState.h"

#include "GameFramework/GameStateBase.h"

void AOutlierArenaPausePlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (AGameStateBase* CurrentGameState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		CurrentGameState->RemovePlayerState(this);
	}
}
