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

	if (PartnerCharacter)
	{
		PartnerCharacter->NotifyBoundaryUI(bSuitDisabledByPartnerBoundary);
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

	// Listen Server: OnRep_SuitDisabledByPartnerBoundary fires only on clients,
	// so directly notify the Partner's UI on the server instance.
	if (PartnerCharacter)
	{
		PartnerCharacter->NotifyBoundaryUI(bDisabled);
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
	if (!HasAuthority() || PlayerRole == NewRole)
	{
		return;
	}

	PlayerRole = NewRole;
	HandlePlayerRoleChanged();
}

void AOutlierPlayerState::SetPairId(int32 NewPairId)
{
	if (!HasAuthority())
	{
		return;
	}

	PairId = NewPairId;
}

void AOutlierPlayerState::SetArenaId(int32 NewArenaId)
{
	if (!HasAuthority())
	{
		return;
	}

	ArenaId = NewArenaId;
}

void AOutlierPlayerState::SetPendingLobbyMatchId(int32 NewPendingLobbyMatchId)
{
	if (!HasAuthority() || PendingLobbyMatchId == NewPendingLobbyMatchId)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PlayerState] SetPendingLobbyMatchId: %d -> %d (%s)"),
		PendingLobbyMatchId, NewPendingLobbyMatchId, *GetPlayerName());

	PendingLobbyMatchId = NewPendingLobbyMatchId;
	HandlePendingLobbyStateChanged();
}

void AOutlierPlayerState::SetPendingLobbyRole(EOutlierPlayerRole NewPendingLobbyRole)
{
	if (!HasAuthority() || PendingLobbyRole == NewPendingLobbyRole)
	{
		return;
	}

	PendingLobbyRole = NewPendingLobbyRole;
	HandlePendingLobbyStateChanged();
}

void AOutlierPlayerState::ClearPendingLobbyState()
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bChanged =
		PendingLobbyMatchId != INDEX_NONE ||
		PendingLobbyRole != EOutlierPlayerRole::None;

	PendingLobbyMatchId = INDEX_NONE;
	PendingLobbyRole = EOutlierPlayerRole::None;

	if (bChanged)
	{
		HandlePendingLobbyStateChanged();
	}
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

	OnPlayerCharactersChanged.Broadcast(this);
}

void AOutlierPlayerState::OnRep_PlayerRole()
{
	HandlePlayerRoleChanged();
}

void AOutlierPlayerState::OnRep_PendingLobbyMatchId()
{
	UE_LOG(LogTemp, Warning, TEXT("[PlayerState] OnRep_PendingLobbyMatchId: %d on CLIENT (%s)"),
		PendingLobbyMatchId, *GetPlayerName());
	HandlePendingLobbyStateChanged();
}

void AOutlierPlayerState::OnRep_PendingLobbyRole()
{
	HandlePendingLobbyStateChanged();
}

void AOutlierPlayerState::HandlePlayerRoleChanged()
{
	OnPlayerRoleChanged.Broadcast(this);
}

void AOutlierPlayerState::HandlePendingLobbyStateChanged()
{
	OnPendingLobbyStateChanged.Broadcast(this);
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
	DOREPLIFETIME(AOutlierPlayerState, ArenaId);
	DOREPLIFETIME(AOutlierPlayerState, PendingLobbyMatchId);
	DOREPLIFETIME(AOutlierPlayerState, PendingLobbyRole);
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
