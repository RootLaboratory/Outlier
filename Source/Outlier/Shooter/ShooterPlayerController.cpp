// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "UI/ShooterAbilityUI.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterMainWidget.h"
#include "OutlierGameMode.h"

AShooterPlayerController::AShooterPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Shooter;
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
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
		ShooterCharacter->Tags.Add(PlayerPawnTag);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ShooterPC] OnPossess"));	
	}

	
}
void AShooterPlayerController::AcknowledgePossession(APawn* P) 
{
	Super::AcknowledgePossession(P);
	BindShooterCharacterDelegates(Cast<AShooterCharacter>(P));
}

void AShooterPlayerController::BindShooterCharacterDelegates(AShooterCharacter* ShooterCharacter)
{
	ShooterCharacter->OnMovementStateChanged.AddDynamic(
		this,
		&AShooterPlayerController::HandleMovementStateChanged
	);

	ShooterCharacter->OnWeaponChanged.AddDynamic(
		this,
		&AShooterPlayerController::OnWeaponChanged
	);
}

void AShooterPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	if (!IsLocalController())
	{
		return;
	}

	BindMainUI();
	BindPostProcessSubSystem();

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
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: not local PC=%s Auth=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: already exists PC=%s UI=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ShooterUIInstance));
		return;
	}

	if (!MainUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: MainUIClass is null PC=%s Class=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetClass()));
		return;
	}

	ShooterUIInstance = CreateWidget<UMainUIBase>(this, MainUIClass);

	if (!ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: CreateWidget returned null PC=%s MainUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(MainUIClass));
		return;
	}

	ShooterUIInstance->AddToViewport();
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterPC] MainUI added PC=%s UI=%s MainUIClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShooterUIInstance),
		*GetNameSafe(MainUIClass));

	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
			UE_LOG(LogTemp, Warning,
				TEXT("[ShooterPC] MainUI registered to UISubsystem PC=%s UI=%s"),
				*GetNameSafe(this),
				*GetNameSafe(ShooterUIInstance));
		}
	}

	if (AbilityUIInstance)
	{
		return;
	}

	if (!AbilityUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: AbilityUIClass is null PC=%s"),
			*GetNameSafe(this));
		return;
	}

	AbilityUIInstance = CreateWidget<UShooterAbilityUI>(this, AbilityUIClass);
	if (!AbilityUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: Create AbilityUI returned null PC=%s AbilityUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(AbilityUIClass));
		return;
	}

	AbilityUIInstance->AddToViewport();
	AbilityUIInstance->SetVisibility(ESlateVisibility::Collapsed);
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterPC] AbilityUI added PC=%s UI=%s AbilityUIClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(AbilityUIInstance),
		*GetNameSafe(AbilityUIClass));
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

void AShooterPlayerController::OnWeaponChanged(EWeaponType NewType)
{
	UShooterMainWidget* ShooterUI = Cast<UShooterMainWidget>(ShooterUIInstance);

	if (ShooterUI)
	{
		ShooterUI->OnChangeWeapon(static_cast<EWidgetWeaponType>(NewType));

	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ShooterUI"));

	}
	
}
