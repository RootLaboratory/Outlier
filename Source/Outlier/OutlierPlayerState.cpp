// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
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
	if (!HasAuthority() || PairId == NewPairId)
	{
		return;
	}

	PairId = NewPairId;
	SetNodeCountInternal(NewPairId == INDEX_NONE ? 0 : FMath::Max(0, InitialNodeCount));
	SetStatAllocatorExitPending(false);
	ForceNetUpdate();
}

bool AOutlierPlayerState::AddNode(int32 Amount)
{
	if (!HasAuthority() || PairId == INDEX_NONE || Amount <= 0)
	{
		return false;
	}

	if (NodeCount > TNumericLimits<int32>::Max() - Amount)
	{
	/*	UE_LOG(LogTemp, Warning,
			TEXT("[PlayerState][Node] Failed to add node: count overflow Player=%s Current=%d Amount=%d"),
			*GetPlayerName(),
			NodeCount,
			Amount);*/
		return false;
	}

	SetNodeCountInternal(NodeCount + Amount);
	return true;
}

bool AOutlierPlayerState::ShareNode(int32 Amount)
{
	if (!HasAuthority() || PairId == INDEX_NONE || Amount <= 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return false;
	}

	const EOutlierPlayerRole PairedRole = IsShooterPlayer()
		? EOutlierPlayerRole::Partner
		: IsPartnerPlayer()
			? EOutlierPlayerRole::Shooter
			: EOutlierPlayerRole::None;
	if (PairedRole == EOutlierPlayerRole::None)
	{
		return false;
	}

	AOutlierPlayerState* PairedPlayerState = nullptr;

	for (APlayerState* Candidate : GameState->PlayerArray)
	{
		AOutlierPlayerState* CandidatePlayerState = Cast<AOutlierPlayerState>(Candidate);
		if (CandidatePlayerState
			&& CandidatePlayerState != this
			&& CandidatePlayerState->GetPairId() == PairId
			&& CandidatePlayerState->GetPlayerRole() == PairedRole)
		{
			PairedPlayerState = CandidatePlayerState;
			break;
		}
	}

	if (!PairedPlayerState)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerState][Node] Failed to share node: paired PlayerState not found Player=%s PairId=%d"),
			*GetPlayerName(),
			PairId);
		return false;
	}

	const int32 PairedAmount = Amount / 2;
	const int32 OwnAmount = PairedAmount + Amount % 2;
	const int32 MaxNodeCount = TNumericLimits<int32>::Max();

	if (NodeCount > MaxNodeCount - OwnAmount
		|| PairedPlayerState->NodeCount > MaxNodeCount - PairedAmount)
	{
	/*	UE_LOG(LogTemp, Warning,
			TEXT("[PlayerState][Node] Failed to share node: count overflow Player=%s Current=%d OwnAmount=%d PairedPlayer=%s PairedCurrent=%d PairedAmount=%d"),
			*GetPlayerName(),
			NodeCount,
			OwnAmount,
			*PairedPlayerState->GetPlayerName(),
			PairedPlayerState->NodeCount,
			PairedAmount);*/

		return false;
	}

	SetNodeCountInternal(NodeCount + OwnAmount);
	PairedPlayerState->SetNodeCountInternal(PairedPlayerState->NodeCount + PairedAmount);

	/*UE_LOG(LogTemp, Log,
		TEXT("[PlayerState][Node] Shared node PairId=%d Player=%s Added=%d Total=%d PairedPlayer=%s Added=%d Total=%d"),
		PairId,
		*GetPlayerName(),
		OwnAmount,
		NodeCount,
		*PairedPlayerState->GetPlayerName(),
		PairedAmount,
		PairedPlayerState->NodeCount);*/

	return true;
}

bool AOutlierPlayerState::ConsumeNode(int32 Amount)
{
	if (!HasAuthority()
		|| PairId == INDEX_NONE
		|| Amount <= 0
		|| NodeCount < Amount)
	{
		return false;
	}

	SetNodeCountInternal(NodeCount - Amount);
	return true;
}

void AOutlierPlayerState::SetStatAllocatorExitPending(bool bPending)
{
	if (!HasAuthority())
	{
		ServerSetStatAllocatorExitPending(bPending);
		return;
	}

	if (bStatAllocatorExitPending == bPending)
	{
		return;
	}

	bStatAllocatorExitPending = bPending;
	HandleStatAllocatorExitPendingChanged();
	ForceNetUpdate();
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

	/*UE_LOG(LogTemp, Warning, TEXT("[PlayerState] SetPendingLobbyMatchId: %d -> %d (%s)"),
		PendingLobbyMatchId, NewPendingLobbyMatchId, *GetPlayerName());*/

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

void AOutlierPlayerState::SetPendingLobbySlotIndex(int32 NewPendingLobbySlotIndex)
{
	if (!HasAuthority() || PendingLobbySlotIndex == NewPendingLobbySlotIndex)
	{
		return;
	}

	PendingLobbySlotIndex = NewPendingLobbySlotIndex;
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
		PendingLobbyRole != EOutlierPlayerRole::None ||
		PendingLobbySlotIndex != INDEX_NONE;

	PendingLobbyMatchId = INDEX_NONE;
	PendingLobbyRole = EOutlierPlayerRole::None;
	PendingLobbySlotIndex = INDEX_NONE;

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

void AOutlierPlayerState::OnRep_PendingLobbySlotIndex()
{
	HandlePendingLobbyStateChanged();
}

void AOutlierPlayerState::OnRep_NodeCount()
{
	OnNodeCountChanged.Broadcast(NodeCount);
}

void AOutlierPlayerState::OnRep_StatAllocatorExitPending()
{
	HandleStatAllocatorExitPendingChanged();
}

void AOutlierPlayerState::OnRep_ActivatedUpgradeNodes()
{
	HandleActivatedUpgradeNodesChanged();
}

void AOutlierPlayerState::ServerSetStatAllocatorExitPending_Implementation(bool bPending)
{
	SetStatAllocatorExitPending(bPending);
}

void AOutlierPlayerState::HandlePlayerRoleChanged()
{
	OnPlayerRoleChanged.Broadcast(this);
}

void AOutlierPlayerState::HandlePendingLobbyStateChanged()
{
	OnPendingLobbyStateChanged.Broadcast(this);
}

void AOutlierPlayerState::HandleStatAllocatorExitPendingChanged()
{
	OnStatAllocatorExitPendingChanged.Broadcast(this);
}

void AOutlierPlayerState::HandleActivatedUpgradeNodesChanged()
{
	OnActivatedUpgradeNodesChanged.Broadcast();
}

void AOutlierPlayerState::SetNodeCountInternal(int32 NewNodeCount)
{
	NewNodeCount = FMath::Max(0, NewNodeCount);
	if (NodeCount == NewNodeCount)
	{
		return;
	}

	NodeCount = NewNodeCount;
	OnNodeCountChanged.Broadcast(NodeCount);
	ForceNetUpdate();
}

void AOutlierPlayerState::SetTemporaryPlayerId(const FGuid& NewPlayerId)
{
	if (!HasAuthority()
		|| !NewPlayerId.IsValid()
		|| (TemporaryPlayerId.IsValid() && TemporaryPlayerId != NewPlayerId))
	{
		return;
	}

	TemporaryPlayerId = NewPlayerId;
	ForceNetUpdate();
}

void AOutlierPlayerState::SetCheckpointData(const FOutlierCheckpointData& NewData)
{
	if (!HasAuthority())
	{
		return;
	}

	CheckpointData = NewData;
}

bool AOutlierPlayerState::AddActivatedUpgradeNode(EOutlierUpgradeRole UpgradeRole, FName RowName)
{
	if (!HasAuthority() || RowName.IsNone())
	{
		return false;
	}

	TArray<FName>* ActivatedNodeIds = nullptr;
	switch (UpgradeRole)
	{
	case EOutlierUpgradeRole::Shooter:
		ActivatedNodeIds = &ShooterActivatedUpgradeNodeIds;
		break;

	case EOutlierUpgradeRole::Partner:
		ActivatedNodeIds = &PartnerActivatedUpgradeNodeIds;
		break;

	default:
		return false;
	}

	if (ActivatedNodeIds->Contains(RowName))
	{
		return false;
	}

	ActivatedNodeIds->Add(RowName);
	HandleActivatedUpgradeNodesChanged();
	ForceNetUpdate();
	return true;
}

const TArray<FName>& AOutlierPlayerState::GetActivatedUpgradeNodeIds(EOutlierUpgradeRole UpgradeRole) const
{
	static const TArray<FName> EmptyActivatedNodeIds;

	switch (UpgradeRole)
	{
	case EOutlierUpgradeRole::Shooter:
		return ShooterActivatedUpgradeNodeIds;

	case EOutlierUpgradeRole::Partner:
		return PartnerActivatedUpgradeNodeIds;

	default:
		return EmptyActivatedNodeIds;
	}
}

void AOutlierPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOutlierPlayerState, PlayerRole);
	DOREPLIFETIME(AOutlierPlayerState, PairId);
	DOREPLIFETIME_CONDITION(AOutlierPlayerState, NodeCount, COND_OwnerOnly); //공유될 필요는 없어서 소유자만 복제
	DOREPLIFETIME(AOutlierPlayerState, bStatAllocatorExitPending);
	DOREPLIFETIME(AOutlierPlayerState, ArenaId);
	DOREPLIFETIME(AOutlierPlayerState, PendingLobbyMatchId);
	DOREPLIFETIME(AOutlierPlayerState, PendingLobbyRole);
	DOREPLIFETIME(AOutlierPlayerState, PendingLobbySlotIndex);
	DOREPLIFETIME(AOutlierPlayerState, CheckpointData);
	DOREPLIFETIME(AOutlierPlayerState, ShooterCharacter);
	DOREPLIFETIME(AOutlierPlayerState, PartnerCharacter);
	DOREPLIFETIME(AOutlierPlayerState, bSuitDisabledByPartnerBoundary);
	DOREPLIFETIME_CONDITION(AOutlierPlayerState, ShooterActivatedUpgradeNodeIds, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AOutlierPlayerState, PartnerActivatedUpgradeNodeIds, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AOutlierPlayerState, TemporaryPlayerId, COND_OwnerOnly);
}


AShooterCharacter* AOutlierPlayerState::GetShooterCharacter() const
{
	return ShooterCharacter;
}

APartnerCharacter* AOutlierPlayerState::GetPartnerCharacter() const
{
	return PartnerCharacter;
}
