// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "LocalPlayerUISubSystem.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

APartnerPlayerController::APartnerPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Partner;
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void APartnerPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void APartnerPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindShooterCharacterDelegates();
	UnbindPlayerStateDelegates();
	Super::EndPlay(EndPlayReason);
}

void APartnerPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindPlayerStateDelegates();
	RefreshShooterUIForRespawnFromPlayerState();
}

void APartnerPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void APartnerPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}

	if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(InPawn))
	{
		PartnerCharacter->Tags.Add(PartnerPawnTag);
	}

	/*if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(InPawn))
	{

		if (IsLocalController())
		{
			UE_LOG(LogTemp, Error, TEXT("TryRefresh"));
			PartnerCharacter->RefreshPostProcessState();
		}
	}*/

	if (IsLocalController())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}
	}

}

void APartnerPlayerController::BindMainUI()
{
	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI skipped: not local PC=%s Auth=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI skipped: already exists PC=%s UI=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ShooterUIInstance));
		return;
	}

	if (!MainUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI failed: MainUIClass is null PC=%s Class=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetClass()));
		return;
	}

	ShooterUIInstance = CreateWidget<UMainUIBase>(this, MainUIClass);
	if (!ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerPC] BindMainUI failed: CreateWidget returned null PC=%s MainUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(MainUIClass));
		return;
	}

	ShooterUIInstance->AddToViewport();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerPC] No GetLocalPlayer"));
		}
	}
}

void APartnerPlayerController::BindPostProcessSubSystem()
{
}

void APartnerPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	if (!IsLocalController())
	{
		return;
	}

	BindMainUI();
	BindPostProcessSubSystem();
}

void APartnerPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	BindMainUI();
	BindPostProcessSubSystem();
	BindPlayerStateDelegates();
	RefreshShooterUIForRespawnFromPlayerState();

	if (IsLocalController())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}
	}

}

void APartnerPlayerController::RefreshShooterUIForRespawnFromPlayerState()
{
	BindShooterCharacterDelegatesFromPlayerState();
}

void APartnerPlayerController::BindPlayerStateDelegates()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState || BoundOutlierPlayerState == OutlierPlayerState)
	{
		return;
	}

	UnbindPlayerStateDelegates();
	BoundOutlierPlayerState = OutlierPlayerState;
	BoundOutlierPlayerState->OnPlayerCharactersChanged.AddUObject(
		this,
		&APartnerPlayerController::HandlePlayerCharactersChanged
	);

}

void APartnerPlayerController::UnbindPlayerStateDelegates()
{
	if (!BoundOutlierPlayerState)
	{
		return;
	}

	BoundOutlierPlayerState->OnPlayerCharactersChanged.RemoveAll(this);
	BoundOutlierPlayerState = nullptr;
}

void APartnerPlayerController::HandlePlayerCharactersChanged(AOutlierPlayerState* ChangedPlayerState)
{
	BindShooterCharacterDelegatesFromPlayerState();
}

void APartnerPlayerController::BindShooterCharacterDelegatesFromPlayerState()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: PlayerState is null PC=%s"), *GetNameSafe(this));
		return;
	}

	AShooterCharacter* ShooterCharacter = OutlierPlayerState->GetShooterCharacter();
	if (!ShooterCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: ShooterCharacter is null PS=%s"), *GetNameSafe(OutlierPlayerState));
		return;
	}

	if (!GetLocalUISubsystem())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerPC] Shooter UI refresh skipped: UISubsystem is null PC=%s"), *GetNameSafe(this));
		return;
	}

	UnbindShooterCharacterDelegates();
	BoundShooterCharacter = ShooterCharacter;

	ShooterCharacter->OnShooterHealthChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterHealthChanged
	);

	ShooterCharacter->OnShooterShieldChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterShieldChanged
	);

	ShooterCharacter->OnShooterPartnerShieldChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterPartnerShieldChanged
	);

	ShooterCharacter->OnShooterConditionChanged.AddUObject(
		this,
		&APartnerPlayerController::HandleShooterConditionChanged
	);

	ShooterCharacter->BroadcastCurrentUIState();
}

void APartnerPlayerController::UnbindShooterCharacterDelegates()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter->OnShooterHealthChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterPartnerShieldChanged.RemoveAll(this);
	BoundShooterCharacter->OnShooterConditionChanged.RemoveAll(this);
	BoundShooterCharacter = nullptr;
}

ULocalPlayerUISubSystem* APartnerPlayerController::GetLocalUISubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
}

void APartnerPlayerController::HandleShooterHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_HealthChanged(CurrentHealth, MaxHealth);
	}
}

void APartnerPlayerController::HandleShooterShieldChanged(float CurrentShield, float MaxShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShieldChanged(CurrentShield, MaxShield);
	}
}

void APartnerPlayerController::HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_PartnerShieldChanged(CurrentPartnerShield, MaxPartnerShield);
	}
}

void APartnerPlayerController::HandleShooterConditionChanged(const FGameplayTag& ConditionTag)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShooterHPStateChanged(ConditionTag);
	}
}
