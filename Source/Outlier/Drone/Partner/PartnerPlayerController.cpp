// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerPlayerController.h"
#include "Drone/Partner/PartnerCharacter.h"

APartnerPlayerController::APartnerPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Partner;
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
}

void APartnerPlayerController::BindPostProcessSubSystem()
{
}
