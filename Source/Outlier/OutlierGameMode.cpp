// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpoint.h"
#include "OutlierGameState.h"
#include "FrontendPlayerController.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Save/OutlierSaveSubSystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

AOutlierGameMode::AOutlierGameMode()
{

}

void AOutlierGameMode::RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint)
{
	if (!Controller || !Checkpoint)
	{
		return;
	}

	AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>();

	if (!PS)
	{
		return;
	}

	FOutlierCheckpointData Data;
	Data.LevelName = FName(*GetWorld()->GetMapName());
	Data.CheckpointId = Checkpoint->GetCheckpointId();

	ApplyCheckpointToPair(PS, Data);
}

void AOutlierGameMode::RefreshPairLinks(AOutlierPlayerState* TriggeringPlayerState)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	AShooterCharacter* Shooter = ShooterPlayerState
		? ShooterPlayerState->GetShooterCharacter()
		: nullptr;

	APartnerCharacter* Partner = PartnerPlayerState
		? PartnerPlayerState->GetPartnerCharacter()
		: nullptr;

	if (!Shooter && PartnerPlayerState)
	{
		Shooter = PartnerPlayerState->GetShooterCharacter();
	}

	if (!Partner && ShooterPlayerState)
	{
		Partner = ShooterPlayerState->GetPartnerCharacter();
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, Shooter, Partner);
}

void AOutlierGameMode::ApplyCheckpointToPair(AOutlierPlayerState* TriggeringPlayerState, const FOutlierCheckpointData& Data)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	const int32 PairId = TriggeringPlayerState->GetPairId();

	if (PairId == INDEX_NONE || !GameState)
	{
		TriggeringPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(TriggeringPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}

		return;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PairPlayerState = Cast<AOutlierPlayerState>(RawPlayerState);
		if (!PairPlayerState || PairPlayerState->GetPairId() != PairId)
		{
			continue;
		}

		PairPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(PairPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}
	}
}

void AOutlierGameMode::HandlePlayerDeath(AShooterCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr)
	{
		if (!PS->GetShooterCharacter())
		{
			PS->SetShooterCharacter(Character);
		}

		PS->SetSuitDisabledByPartnerBoundary(false);
	}

	Character->DetachFromControllerPendingDestroy();
	
	RespawnPairAtCheckpoint(Controller);
}

void AOutlierGameMode::StartMatchedPair(AController* FirstController, AController* SecondController, int32 PairId, int32 ArenaId, EOutlierPlayerRole FirstRole, EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return;
	}

	AOutlierPlayerState* FirstPS = FirstController->GetPlayerState<AOutlierPlayerState>();
	AOutlierPlayerState* SecondPS = SecondController->GetPlayerState<AOutlierPlayerState>();

	if (!FirstPS || !SecondPS)
	{
		return;
	}

	FirstPS->SetPairId(PairId);
	FirstPS->SetArenaId(ArenaId);
	FirstPS->SetPlayerRole(FirstRole);

	SecondPS->SetPairId(PairId);
	SecondPS->SetArenaId(ArenaId);
	SecondPS->SetPlayerRole(SecondRole);

	AController* ShooterController =
		FirstRole == EOutlierPlayerRole::Shooter
		? FirstController
		: SecondController;

	AController* PartnerController =
		FirstRole == EOutlierPlayerRole::Partner
		? FirstController
		: SecondController;

	AOutlierPlayerState* ShooterPS =
		ShooterController->GetPlayerState<AOutlierPlayerState>();

	AOutlierPlayerState* PartnerPS =
		PartnerController->GetPlayerState<AOutlierPlayerState>();

	FTransform ShooterSpawn;
	FTransform PartnerSpawn;

	if (!ResolveArenaSpawnTransforms(ArenaId, ShooterSpawn, PartnerSpawn))
	{
		AActor* FallbackStart = FindPlayerStart(ShooterController);
		ShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
		PartnerSpawn = ShooterSpawn;
		PartnerSpawn.AddToTranslation(ShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	AShooterCharacter* Shooter = GetWorld()->SpawnActor<AShooterCharacter>(
		ShooterClass,
		ShooterSpawn
	);

	APartnerCharacter* Partner = GetWorld()->SpawnActor<APartnerCharacter>(
		PartnerClass,
		PartnerSpawn
	);

	if (ShooterController && Shooter)
	{
		ShooterController->Possess(Shooter);
	}

	if (PartnerController && Partner)
	{
		PartnerController->Possess(Partner);
	}

	RegisterSpawnedPair(ShooterPS, PartnerPS, Shooter, Partner);
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


void AOutlierGameMode::RespawnPairAtCheckpoint(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPlayerState = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	if (!ShooterPlayerState)
	{
		ShooterPlayerState = TriggeringPlayerState;
	}

	FTransform SpawnTransform;
	if (!ResolveCheckpointTransform(GetControllerFromPlayerState(ShooterPlayerState), SpawnTransform))
	{
		AActor* PlayerStart = FindPlayerStart(Controller);
		SpawnTransform = PlayerStart
			? PlayerStart->GetActorTransform()
			: FTransform::Identity;
	}

	AShooterCharacter* OldShooter = ShooterPlayerState->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPlayerState->GetPartnerCharacter();

	if (!OldPartner && PartnerPlayerState)
	{
		OldPartner = PartnerPlayerState->GetPartnerCharacter();
	}

	ShooterPlayerState->SetShooterCharacter(nullptr);
	ShooterPlayerState->SetPartnerCharacter(nullptr);
	ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(nullptr);
		PartnerPlayerState->SetPartnerCharacter(nullptr);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (OldShooter)
	{
		OldShooter->CleanupOwnedWeapons();
		OldShooter->Destroy();
	}

	if (OldPartner)
	{
		OldPartner->Destroy();
	}

	AShooterCharacter* NewShooter = nullptr;
	if (ShooterClass)
	{
		NewShooter = GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, SpawnTransform);
	}
	else if (DefaultPawnClass)
	{
		NewShooter = Cast<AShooterCharacter>(
			GetWorld()->SpawnActor<APawn>(DefaultPawnClass, SpawnTransform)
		);
	}

	FTransform PartnerSpawnTransform = SpawnTransform;
	PartnerSpawnTransform.AddToTranslation(
		SpawnTransform.GetRotation().GetRightVector() * 150.0f
	);

	APartnerCharacter* NewPartner = PartnerClass
		? GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, PartnerSpawnTransform)
		: nullptr;

	if (AController* ShooterController = GetControllerFromPlayerState(ShooterPlayerState))
	{
		if (NewShooter)
		{
			ShooterController->Possess(NewShooter);
		}
	}

	if (AController* PartnerController = GetControllerFromPlayerState(PartnerPlayerState))
	{
		if (NewPartner)
		{
			PartnerController->Possess(NewPartner);
		}
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, NewShooter, NewPartner);
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

AOutlierPlayerState* AOutlierGameMode::FindPairPlayerState(int32 PairId, EOutlierPlayerRole PlayerRole) const
{
	if (PairId == INDEX_NONE || !GameState)
	{
		return nullptr;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PS = Cast<AOutlierPlayerState>(RawPlayerState);
		if (PS && PS->GetPairId() == PairId && PS->GetPlayerRole() == PlayerRole)
		{
			return PS;
		}
	}

	return nullptr;
}

AController* AOutlierGameMode::GetControllerFromPlayerState(AOutlierPlayerState* PlayerState) const
{
	return PlayerState ? Cast<AController>(PlayerState->GetOwner()) : nullptr;
}

void AOutlierGameMode::RegisterSpawnedPair(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	AShooterCharacter* Shooter,
	APartnerCharacter* Partner)
{
	if (ShooterPlayerState)
	{
		ShooterPlayerState->SetShooterCharacter(Shooter);
		ShooterPlayerState->SetPartnerCharacter(Partner);
		ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(Shooter);
		PartnerPlayerState->SetPartnerCharacter(Partner);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}
}

bool AOutlierGameMode::ResolveArenaSpawnTransforms(int32 ArenaId, FTransform& OutShooterSpawn, FTransform& OutPartnerSpawn) const
{
	// 예시:
	// AOutlierArenaSpawnPoint 같은 Actor를 만들고
	// ArenaId + Role 기준으로 찾기

	ULevel* ArenaLevel = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
		}
	}

	if (!ArenaLevel)
	{
		return false;
	}

	TArray<AActor*> PlayerStartActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(),
		APlayerStart::StaticClass(),
		PlayerStartActors
	);

	APlayerStart* ShooterStart = nullptr;
	APlayerStart* PartnerStart = nullptr;
	TArray<APlayerStart*> ArenaStarts;

	for (AActor* Actor : PlayerStartActors)
	{
		APlayerStart* PlayerStart = Cast<APlayerStart>(Actor);
		if (!PlayerStart || PlayerStart->GetLevel() != ArenaLevel)
		{
			continue;
		}

		ArenaStarts.Add(PlayerStart);

		const FName StartTag = PlayerStart->PlayerStartTag;
		if (!ShooterStart && (StartTag == TEXT("Shooter") || StartTag == TEXT("Player1") || StartTag == TEXT("1P")))
		{
			ShooterStart = PlayerStart;
		}
		else if (!PartnerStart && (StartTag == TEXT("Partner") || StartTag == TEXT("Player2") || StartTag == TEXT("2P")))
		{
			PartnerStart = PlayerStart;
		}
	}

	if (!ShooterStart && ArenaStarts.Num() > 0)
	{
		ShooterStart = ArenaStarts[0];
	}

	if (!PartnerStart && ArenaStarts.Num() > 1)
	{
		PartnerStart = ArenaStarts[1];
	}

	if (!ShooterStart)
	{
		return false;
	}

	OutShooterSpawn = ShooterStart->GetActorTransform();
	if (PartnerStart)
	{
		OutPartnerSpawn = PartnerStart->GetActorTransform();
	}
	else
	{
		OutPartnerSpawn = OutShooterSpawn;
		OutPartnerSpawn.AddToTranslation(OutShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	return true;

}
