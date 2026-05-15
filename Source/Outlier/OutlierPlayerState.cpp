// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"

void AOutlierPlayerState::OnRep_CheckpointData()
{

}

void AOutlierPlayerState::OnRep_ShooterCharacter()
{
	RefreshCharacterLinks();
}

void AOutlierPlayerState::OnRep_PartnerCharacter()
{
	RefreshCharacterLinks();
}

void AOutlierPlayerState::OnRep_SuitDisabledByPartnerBoundary()
{
	if (ShooterCharacter)
	{
		ShooterCharacter->SetSuitDisabledByPartnerBoundary(bSuitDisabledByPartnerBoundary);
	}
}

void AOutlierPlayerState::SetShooterCharacter(AShooterCharacter* NewShooter)
{
	if (!HasAuthority())
	{
		return;
	}

	ShooterCharacter = NewShooter;
	RefreshCharacterLinks();
}

void AOutlierPlayerState::SetPartnerCharacter(APartnerCharacter* NewPartner)
{
	if (!HasAuthority())
	{
		return;
	}

	PartnerCharacter = NewPartner;
	RefreshCharacterLinks();
}

void AOutlierPlayerState::SetSuitDisabledByPartnerBoundary(bool bDisabled)
{
	if (!HasAuthority() || bSuitDisabledByPartnerBoundary == bDisabled)
	{
		return;
	}

	bSuitDisabledByPartnerBoundary = bDisabled;

	if (ShooterCharacter)
	{
		ShooterCharacter->SetSuitDisabledByPartnerBoundary(bDisabled);
	}
}

float AOutlierPlayerState::GetPartnerDistance() const
{
	if (!ShooterCharacter || !PartnerCharacter)
	{
		return 0.0f;
	}

	return FVector::Dist(
		ShooterCharacter->GetActorLocation(),
		PartnerCharacter->GetActorLocation()
	);
}

void AOutlierPlayerState::SetPlayerRole(EOutlierPlayerRole NewRole)
{
	if (!HasAuthority())
	{
		return;
	}

	PlayerRole = NewRole;
}

void AOutlierPlayerState::SetPairId(int32 NewPairId)
{
	if (!HasAuthority())
	{
		return;
	}

	PairId = NewPairId;
}

void AOutlierPlayerState::RefreshCharacterLinks()
{
	if (ShooterCharacter)
	{
		ShooterCharacter->SetPartnerCharacter(PartnerCharacter);
		ShooterCharacter->SetSuitDisabledByPartnerBoundary(bSuitDisabledByPartnerBoundary);
	}

	if (PartnerCharacter)
	{
		PartnerCharacter->SetShooterCharacter(ShooterCharacter);
	}
}

void AOutlierPlayerState::SetCheckpointData(const FOutlierCheckpointData& NewData)
{
	if (!HasAuthority())
	{
		return;
	}

	CheckpointData = NewData;
}

void AOutlierPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOutlierPlayerState, PlayerRole);
	DOREPLIFETIME(AOutlierPlayerState, PairId);
	DOREPLIFETIME(AOutlierPlayerState, CheckpointData);
	DOREPLIFETIME(AOutlierPlayerState, ShooterCharacter);
	DOREPLIFETIME(AOutlierPlayerState, PartnerCharacter);
	DOREPLIFETIME(AOutlierPlayerState, bSuitDisabledByPartnerBoundary);
}


AShooterCharacter* AOutlierPlayerState::GetShooterCharacter() const
{
	return ShooterCharacter;
}

APartnerCharacter* AOutlierPlayerState::GetPartnerCharacter() const
{
	return PartnerCharacter;
}
