// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "OutlierPlayerState.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"

void UPartnerCharacterComponentBase::BeginPlay()
{
	Super::BeginPlay();

	RefreshCharacterRefsFromPlayerState();
}

void UPartnerCharacterComponentBase::RefreshCharacterRefsFromPlayerState()
{
	PartnerCharacter = Cast<APartnerCharacter>(GetOwner());
	if (!PartnerCharacter)
	{
		ShooterCharacter = nullptr;
		return;
	}

	AOutlierPlayerState* PS = PartnerCharacter->GetPlayerState<AOutlierPlayerState>();
	if (!PS)
	{
		ShooterCharacter = nullptr;
		return;
	}

	ShooterCharacter = PS->GetShooterCharacter();
}
