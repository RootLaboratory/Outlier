// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerController.h"

#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "ShooterCharacter.h"

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	BindMainUI();
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}

	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// Mark the currently possessed pawn so gameplay systems can identify it.
		ShooterCharacter->Tags.Add(PlayerPawnTag);
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// Find a spawn point to respawn the player.
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(AActor::GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() <= 0)
	{
		return;
	}

	// Choose a random player start.
	AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];
	const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

	// Spawn a replacement pawn and repossess it.
	if (AShooterCharacter* RespawnedCharacter = AActor::GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
	{
		Possess(RespawnedCharacter);
	}
}



void AShooterPlayerController::BindMainUI()
{

	UE_LOG(LogTemp, Warning, TEXT("BindMainUI"));


	if ( !MainUIClass || ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cant InitializeMainUI"));

		return;
	}

	ShooterUIInstance = CreateWidget<UMainUIBase>(this, MainUIClass);
	if (!ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cant ShooterUIInstance"));

		return;
	}

	ShooterUIInstance->AddToViewport();

		if (ULocalPlayer* LP = this->GetLocalPlayer())
		{
			if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
			{
				UISubsystem->RegisterMainUI(ShooterUIInstance);

				//UISubsystem->PartnerCameraBind(CaptureComponent); //Main 끝내고.

			}
		}



}




