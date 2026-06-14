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
	BindMainUI();
}

void AShooterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindShooterCharacterDelegates();
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

	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(P))
	{
		Shooter->RefreshUIForRespawn();
	}
}

void AShooterPlayerController::BindShooterCharacterDelegates(AShooterCharacter* ShooterCharacter)
{
	UnbindShooterCharacterDelegates();

	if (!ShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter = ShooterCharacter;

	ShooterCharacter->OnMovementStateChanged.AddDynamic(
		this,
		&AShooterPlayerController::HandleMovementStateChanged
	);

	ShooterCharacter->OnWeaponChanged.AddDynamic(
		this,
		&AShooterPlayerController::OnWeaponChanged
	);

	ShooterCharacter->OnShooterHealthChanged.AddUObject(
		this,
		&AShooterPlayerController::HandleShooterHealthChanged
	);

	ShooterCharacter->OnShooterShieldChanged.AddUObject(
		this,
		&AShooterPlayerController::HandleShooterShieldChanged
	);

	ShooterCharacter->OnShooterPartnerShieldChanged.AddUObject(
		this,
		&AShooterPlayerController::HandleShooterPartnerShieldChanged
	);

	ShooterCharacter->OnShooterConditionChanged.AddUObject(
		this,
		&AShooterPlayerController::HandleShooterConditionChanged
	);
}

void AShooterPlayerController::UnbindShooterCharacterDelegates()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter->OnMovementStateChanged.RemoveAll(this);
	BoundShooterCharacter->OnWeaponChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterHealthChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterPartnerShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterConditionChanged.RemoveAll(this);
	BoundShooterCharacter = nullptr;
}

ULocalPlayerUISubSystem* AShooterPlayerController::GetLocalUISubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
}

void AShooterPlayerController::HandleShooterHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_HealthChanged(CurrentHealth, MaxHealth);
	}
}

void AShooterPlayerController::HandleShooterShieldChanged(float CurrentShield, float MaxShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShieldChanged(CurrentShield, MaxShield);
	}
}

void AShooterPlayerController::HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_PartnerShieldChanged(CurrentPartnerShield, MaxPartnerShield);
	}
}

void AShooterPlayerController::HandleShooterConditionChanged(const FGameplayTag& ConditionTag)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShooterHPStateChanged(ConditionTag);
	}
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
		TEXT("[ShooterPC] MainUI added to viewport PC=%s UI=%s MainUIClass=%s"),
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
	AbilityUIInstance->OnAbilitySelected.AddDynamic(
		this,
		&AShooterPlayerController::HandleAbilitySelected
	);
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
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnCurrentWeaponChanged(static_cast<EWidgetWeaponType>(NewType));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Shooter UI subsystem is not ready"));
	}
	
}

void AShooterPlayerController::HandleAbilitySelected(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnCurrentAbilityChanged(AbilityTag);
	}
}
