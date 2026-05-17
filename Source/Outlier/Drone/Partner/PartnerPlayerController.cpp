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

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
		}
	}
}

void APartnerPlayerController::BindPostProcessSubSystem()
{
}

void APartnerPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	BindMainUI();
	BindPostProcessSubSystem();
}


