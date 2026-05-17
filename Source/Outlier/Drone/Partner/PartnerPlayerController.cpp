// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "FirstPerson/FirstPersonPlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "LocalPlayerUISubSystem.h"

APartnerPlayerController::APartnerPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Partner;
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void APartnerPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] PartnerPC BeginPlay: %s Pawn=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetPawn())
	);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierCameraFeel] PartnerPC CameraManager: Class=%s Instance=%s Expected=%s"),
		*GetNameSafe(PlayerCameraManagerClass),
		*GetNameSafe(PlayerCameraManager),
		*GetNameSafe(AFirstPersonPlayerCameraManager::StaticClass())
	);

	BindMainUI();
	BindPostProcessSubSystem();
}

void APartnerPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void APartnerPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] PartnerPC OnPossess: Pawn=%s Class=%s"),
		*GetNameSafe(InPawn),
		InPawn ? *GetNameSafe(InPawn->GetClass()) : TEXT("None")
	);

	if (!InPawn)
	{
		return;
	}

	if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(InPawn))
	{
		// Mark the currently possessed pawn so gameplay systems can identify it.
		PartnerCharacter->Tags.Add(PartnerPawnTag);
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
	UE_LOG(LogTemp, Warning,
		TEXT("[PartnerPC] MainUI added PC=%s UI=%s MainUIClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShooterUIInstance),
		*GetNameSafe(MainUIClass));

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerPC] MainUI registered to UISubsystem PC=%s UI=%s"),
				*GetNameSafe(this),
				*GetNameSafe(ShooterUIInstance));
		}
	}
}

void APartnerPlayerController::BindPostProcessSubSystem()
{
}

void APartnerPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	UE_LOG(LogTemp, Warning,
		TEXT("[PartnerPC] ReceivedPlayer PC=%s Local=%d Auth=%d Pawn=%s PS=%s MainUIClass=%s UI=%s"),
		*GetNameSafe(this),
		IsLocalController(),
		HasAuthority(),
		*GetNameSafe(GetPawn()),
		*GetNameSafe(PlayerState),
		*GetNameSafe(MainUIClass),
		*GetNameSafe(ShooterUIInstance)
	);

	BindMainUI();
	BindPostProcessSubSystem();
}

void APartnerPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	UE_LOG(LogTemp, Warning,
		TEXT("[PartnerPC] AcknowledgePossession PC=%s Local=%d Auth=%d Pawn=%s MainUIClass=%s UI=%s"),
		*GetNameSafe(this),
		IsLocalController(),
		HasAuthority(),
		*GetNameSafe(P),
		*GetNameSafe(MainUIClass),
		*GetNameSafe(ShooterUIInstance)
	);

	BindMainUI();
	BindPostProcessSubSystem();
}
