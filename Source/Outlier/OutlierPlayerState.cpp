// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierPlayerState.h"
#include "LocalPlayerUISubSystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
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

	// 캐릭터 포인터가 슈트 플래그보다 늦게 도착하면 OnRep_AcquiredSuit 때는 넘길 곳이 없다.
	// 링크가 붙는 이 시점에 현재 상태를 한 번 더 투영한다.
	RefreshPairSuitUI();
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

void AOutlierPlayerState::OnRep_PendingPresetSelection()
{
	HandlePendingPresetSelectionChanged();
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

void AOutlierPlayerState::HandlePendingPresetSelectionChanged()
{
	OnPendingPresetSelectionChanged.Broadcast(this);
}

void AOutlierPlayerState::SetPendingPresetSelection(FName NewStageId)
{
	if (!HasAuthority() || PendingPresetSelection == NewStageId)
	{
		return;
	}

	PendingPresetSelection = NewStageId;
	HandlePendingPresetSelectionChanged();
	ForceNetUpdate();
}

void AOutlierPlayerState::FlushActivatedUpgradeNodes(int32 NewNodeCount)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 ClearedShooterNodes = ShooterActivatedUpgradeNodeIds.Num();
	const int32 ClearedPartnerNodes = PartnerActivatedUpgradeNodeIds.Num();
	const bool bHadNodes = ClearedShooterNodes > 0 || ClearedPartnerNodes > 0;
	const int32 PreviousNodeCount = NodeCount;

	ShooterActivatedUpgradeNodeIds.Reset();
	PartnerActivatedUpgradeNodeIds.Reset();

	if (bHadNodes)
	{
		HandleActivatedUpgradeNodesChanged();
		ForceNetUpdate();
	}

	SetNodeCountInternal(NewNodeCount);

	// NodeCount가 "안 바뀐 것처럼" 보이는 경우가 두 가지다: 요청값이 0으로 내려온 경우와
	// 이미 같은 값이라 SetNodeCountInternal이 조용히 조기 반환한 경우. 둘을 구분해서 남긴다.
	UE_LOG(LogTemp, Display,
		TEXT("[PresetNode] Flush PS=%s Pair=%d NodeCount %d -> %d (requested=%d%s) ClearedNodes=%d/%d"),
		*GetPlayerName(),
		PairId,
		PreviousNodeCount,
		NodeCount,
		NewNodeCount,
		PreviousNodeCount == NodeCount ? TEXT(", unchanged") : TEXT(""),
		ClearedShooterNodes,
		ClearedPartnerNodes);
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

void AOutlierPlayerState::SetAcquiredSuit(bool Acquire)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bHasAcquiredSuit == Acquire)
	{
		return;
	}

	bHasAcquiredSuit = Acquire;

	// 리슨 호스트는 자기 값 변경에 OnRep 이 오지 않으므로 여기서 직접 투영한다.
	OnRep_AcquiredSuit();
	ForceNetUpdate();
}

void AOutlierPlayerState::OnRep_AcquiredSuit()
{
	RefreshPairSuitUI();
}

void AOutlierPlayerState::RefreshPairSuitUI()
{
	// 페어의 양쪽 캐릭터에 그대로 넘긴다. "로컬인가"는 각 캐릭터가 스스로 판단하므로
	// 여기서 로컬 플레이어를 찾아낼 필요가 없다 (리슨에서 호스트를 잘못 집던 원인).
	if (AShooterCharacter* Shooter = GetShooterCharacter())
	{
		Shooter->RefreshShooterSuitUI();
	}

	if (APartnerCharacter* Partner = GetPartnerCharacter())
	{
		Partner->RefreshPartnerSuitUI();
	}
}

bool AOutlierPlayerState::IsPairSuitAcquired() const
{
	if (bHasAcquiredSuit)
	{
		return true;
	}

	// Partner PlayerState 에는 플래그가 서지 않는다. 짝의 Shooter 를 거쳐 그쪽 PS 를 본다.
	// ShooterCharacter 는 양쪽 PlayerState 에 모두 복제되므로 Partner 클라에서도 닿는다.
	if (const AShooterCharacter* Shooter = GetShooterCharacter())
	{
		if (const AOutlierPlayerState* ShooterPS = Shooter->GetPlayerState<AOutlierPlayerState>())
		{
			return ShooterPS->GetAcquiredSuit();
		}
	}

	return false;
}

void AOutlierPlayerState::SetSuitMeshes(USkeletalMesh* FirstPersonMesh, USkeletalMesh* ThirdPersonMesh)
{
	if (!HasAuthority())
	{
		return;
	}

	SuitFirstPersonMesh = FirstPersonMesh;
	SuitThirdPersonMesh = ThirdPersonMesh;
}

bool AOutlierPlayerState::GetAcquiredSuit() const
{
	return bHasAcquiredSuit;
}

void AOutlierPlayerState::SetLoadoutSnapshot(const FOutlierLoadoutSnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}

	LoadoutSnapshot = NewSnapshot;
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
	DOREPLIFETIME(AOutlierPlayerState, PendingPresetSelection);
	// 클라가 읽어야 한다 — MainWidget 활성화 여부가 이 값에 걸린다.
	DOREPLIFETIME(AOutlierPlayerState, bHasAcquiredSuit);
}


AShooterCharacter* AOutlierPlayerState::GetShooterCharacter() const
{
	return ShooterCharacter;
}

APartnerCharacter* AOutlierPlayerState::GetPartnerCharacter() const
{
	return PartnerCharacter;
}
