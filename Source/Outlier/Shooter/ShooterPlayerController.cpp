// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "UI/ShooterAbilityUI.h"
#include "ShooterCharacter.h"
#include "OutlierGameMode.h"

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

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// Mark the currently possessed pawn so gameplay systems can identify it.
		ShooterCharacter->Tags.Add(PlayerPawnTag);
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
	if (!AbilityUIClass || AbilityUIInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cant InitializeAbilityUI"));
		return;
	}

	AbilityUIInstance = CreateWidget<UShooterAbilityUI>(this, AbilityUIClass);
	if (!AbilityUIInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cant AbilityUIInstance"));
		return;
	}

	AbilityUIInstance->AddToViewport();
	AbilityUIInstance->SetVisibility(ESlateVisibility::Collapsed);
}

void AShooterPlayerController::BindPostProcessSubSystem()
{
	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
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
