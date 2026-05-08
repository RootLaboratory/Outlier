// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerController.h"

#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "ShooterCharacter.h"

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
	BindMainUI();
	BindPostProcessSubSystem();

	//슬라이드 1P 지정 콜백으로 하겠지만 분리 예정.
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetCharacter());
	if (ShooterCharacter)
	{
		ShooterCharacter->OnMovementStateChanged.AddDynamic(this, &AShooterPlayerController::HandleMovementStateChanged);
	}
}

void AShooterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupPossessedShooterWeapons();

	Super::EndPlay(EndPlayReason);
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
	if (HasAuthority())
	{
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(DestroyedActor))
		{
			ShooterCharacter->CleanupOwnedWeapons();
		}
	}

	if (!HasAuthority() || !CharacterClass || IsPendingKillPending())
	{
		return;
	}

	// Find a spawn point to respawn the player.
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() <= 0)
	{
		return;
	}

	// Choose a random player start.
	AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];
	const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

	// Spawn a replacement pawn and repossess it.
	if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
	{
		Possess(RespawnedCharacter);
	}
}

void AShooterPlayerController::CleanupPossessedShooterWeapons()
{
	if (!HasAuthority())
	{
		return;
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		ShooterCharacter->CleanupOwnedWeapons();
	}
}

void AShooterPlayerController::BindMainUI()
{
	if (!IsLocalController())
	{
		return;
	}

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
		}
	}
}

void AShooterPlayerController::BindPostProcessSubSystem()
{
	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			//PPSubsystem->ActivateChromaticAberration();
			//PPSubsystem->SetDualKawaseBlurEnabled(true);
			//PPSubsystem->SetDualKawaseBlurRadius(6.0f);
			//PPSubsystem->ActivateSlideState();
		}
	}
}

void AShooterPlayerController::HandleMovementStateChanged(EMovementState NewState)
{
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			//UE_LOG(LogTemp, Error, TEXT("HandleMovementStateChanged %d"), NewState));
			switch (NewState)
			{
			case EMovementState::Jump:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Jump);
				break;
			case EMovementState::Slide:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Slide);
				break;
			case EMovementState::Walk:
			case EMovementState::Run:
			case EMovementState::Crouch:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Move);
				break;
			default:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Idle);
				break;
			}
		}
	}
}
