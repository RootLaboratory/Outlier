// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "OutlierCheckpoint.h"
#include "OutlierGameState.h"
#include "FrontendPlayerController.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

AOutlierGameMode::AOutlierGameMode()
{

}

void AOutlierGameMode::RegisterCheckpoint(AShooterCharacter* Character, AOutlierCheckpoint* Checkpoint)
{
	if (!Character || !Checkpoint)
	{
		return;
	}

	AController* Controller = Character->GetController();
	AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (!PS)
	{
		return;
	}

	FOutlierCheckpointData Data;
	Data.LevelName = FName(*GetWorld()->GetMapName());
	Data.CheckpointId = Checkpoint->GetCheckpointId();

	PS->SetCheckpointData(Data);

	if (UOutlierSaveSubSystem* SaveSubsystem =
		GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
	{
		SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
	}
}

void AOutlierGameMode::HandlePlayerDeath(AShooterCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	AController* Controller = Character->GetController();
	Character->DetachFromControllerPendingDestroy();
	Character->CleanupOwnedWeapons();
	Character->Destroy();
	
	RespawnPlayerAtCheckpoint(Controller);
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


void AOutlierGameMode::RespawnPlayerAtCheckpoint(AController* Controller)
{
	if (!Controller || !DefaultPawnClass)
	{
		return;
	}

	FTransform SpawnTransform;
	if (!ResolveCheckpointTransform(Controller, SpawnTransform))
	{
		AActor* PlayerStart = FindPlayerStart(Controller);
		SpawnTransform = PlayerStart
			? PlayerStart->GetActorTransform()
			: FTransform::Identity;
	}

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(DefaultPawnClass, SpawnTransform);
	if (NewPawn)
	{
		Controller->Possess(NewPawn);
	}
}

bool AOutlierGameMode::ResolveCheckpointTransform(AController* Controller, FTransform& OutTransform) const
{
	const AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (!PS || !PS->GetCheckpointData().IsValid())
	{
		return false;
	}

	const FOutlierCheckpointData& Data = PS->GetCheckpointData();

	TArray<AActor*> Checkpoints;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(),
		AOutlierCheckpoint::StaticClass(),
		Checkpoints
	);

	for (AActor* Actor : Checkpoints)
	{
		const AOutlierCheckpoint* Checkpoint = Cast<AOutlierCheckpoint>(Actor);
		if (Checkpoint && Checkpoint->GetCheckpointId() == Data.CheckpointId)
		{
			OutTransform = Checkpoint->GetSpawnTransform();
			return true;
		}
	}

	return false;
}

FString AOutlierGameMode::GetPlayerSaveId(AController* Controller) const
{
	if (!Controller)
	{
		return TEXT("InvalidPlayer");
	}

	if (APlayerState* PS = Controller->PlayerState)
	{
		const FString PlayerName = PS->GetPlayerName();
		if (!PlayerName.IsEmpty())
		{
			return PlayerName;
		}

		return FString::Printf(TEXT("Player_%d"), PS->GetPlayerId());
	}

	return Controller->GetName();
}
