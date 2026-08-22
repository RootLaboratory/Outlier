#include "Upgrade/OutlierUpgradeComponent.h"

#include "Engine/DataTable.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Upgrade/OutlierUpgradeSetData.h"

UOutlierUpgradeComponent::UOutlierUpgradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UOutlierUpgradeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UpgradeSetData)
	{
		Role = UpgradeSetData->UpgradeRole;
		UpgradeDataTable = UpgradeSetData->UpgradeDataTable;
	}

	RebuildNodeCache();
}

void UOutlierUpgradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOutlierUpgradeComponent, ActivatedNodeIds);
}

void UOutlierUpgradeComponent::SetUpgradeRole(EOutlierUpgradeRole InRole)
{
	if (Role == InRole)
	{
		RefreshActivatedNodesFromPlayerState();
		return;
	}

	Role = InRole;
	RebuildNodeCache();
}

void UOutlierUpgradeComponent::SetUpgradeDataTable(UDataTable* InDataTable)
{
	if (UpgradeDataTable == InDataTable)
	{
		RefreshActivatedNodesFromPlayerState();
		return;
	}

	UpgradeDataTable = InDataTable;
	RebuildNodeCache();
}

void UOutlierUpgradeComponent::SetUpgradeSetData(UOutlierUpgradeSetData* InUpgradeSetData)
{
	if (UpgradeSetData == InUpgradeSetData)
	{
		RefreshActivatedNodesFromPlayerState();
		RebuildRuntimeSets();
		RebuildUnlockedNodes();
		OnUpgradeStateChanged.Broadcast();
		return;
	}

	UpgradeSetData = InUpgradeSetData;
	if (UpgradeSetData)
	{
		Role = UpgradeSetData->UpgradeRole;
		UpgradeDataTable = UpgradeSetData->UpgradeDataTable;
	}

	RebuildNodeCache();
}

void UOutlierUpgradeComponent::RebuildNodeCache()
{
	NodeRowsByRowName.Reset();
	RowNamesByNodeId.Reset();
	RowNamesByTreeNodeId.Reset();

	if (UpgradeDataTable)
	{
		for (const TPair<FName, uint8*>& Pair : UpgradeDataTable->GetRowMap())
		{
			const FOutlierUpgradeNodeRow* Row = reinterpret_cast<const FOutlierUpgradeNodeRow*>(Pair.Value);
			if (!Row)
			{
				continue;
			}

			if (Role != EOutlierUpgradeRole::None && Row->Role != Role)
			{
				continue;
			}

			NodeRowsByRowName.Add(Pair.Key, *Row);
			if (!Row->NodeId.IsNone())
			{
				RowNamesByNodeId.Add(Row->NodeId, Pair.Key);
			}

			if (!Row->TreeId.IsNone() && !Row->NodeId.IsNone())
			{
				RowNamesByTreeNodeId.Add(BuildTreeNodeLookupKey(Row->TreeId, Row->NodeId), Pair.Key);
			}
		}
	}

	RefreshActivatedNodesFromPlayerState();
	RebuildRuntimeSets();
	RebuildUnlockedNodes();
	SyncActiveTagsToAbilityComponent();
	OnUpgradeStateChanged.Broadcast();
}

void UOutlierUpgradeComponent::RefreshActivatedNodesFromPlayerState()
{
	if (Role == EOutlierUpgradeRole::None)
	{
		return;
	}

	const AOutlierPlayerState* PlayerState = GetOwningOutlierPlayerState();
	if (!PlayerState)
	{
		return;
	}

	ActivatedNodeIds = PlayerState->GetActivatedUpgradeNodeIds(Role);
}

bool UOutlierUpgradeComponent::TryActivateNode(FName NodeIdOrRowName)
{
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			ServerTryActivateNode(NodeIdOrRowName);
			return true;
		}
	}

	return ActivateNodeInternal(NodeIdOrRowName);
}

bool UOutlierUpgradeComponent::TryActivateNodeForPlayerState(FName NodeIdOrRowName, AOutlierPlayerState* InPlayerState)
{
	if (AActor* Owner = GetOwner())
	{
		if (!Owner->HasAuthority())
		{
			ServerTryActivateNode(NodeIdOrRowName);
			return true;
		}
	}

	return ActivateNodeInternal(NodeIdOrRowName, InPlayerState);
}

bool UOutlierUpgradeComponent::CanActivateNode(FName NodeIdOrRowName) const
{
	FName RowName = NAME_None;
	const FOutlierUpgradeNodeRow* Row = FindNodeRow(NodeIdOrRowName, &RowName);
	if (!Row || ActivatedNodeSet.Contains(RowName) || !IsNodeUnlockedInternal(RowName))
	{
		return false;
	}

	return !bConsumeNodeCost || GetCurrentNodeCount() >= Row->Cost;
}

EOutlierUpgradeNodeState UOutlierUpgradeComponent::GetNodeState(FName NodeIdOrRowName) const
{
	FName RowName = NAME_None;
	if (!ResolveNodeRowName(NodeIdOrRowName, RowName))
	{
		return EOutlierUpgradeNodeState::Locked;
	}

	if (ActivatedNodeSet.Contains(RowName))
	{
		return EOutlierUpgradeNodeState::Activated;
	}

	return IsNodeUnlockedInternal(RowName)
		? EOutlierUpgradeNodeState::Unlocked
		: EOutlierUpgradeNodeState::Locked;
}

bool UOutlierUpgradeComponent::IsNodeActivated(FName NodeIdOrRowName) const
{
	FName RowName = NAME_None;
	return ResolveNodeRowName(NodeIdOrRowName, RowName) && ActivatedNodeSet.Contains(RowName);
}

bool UOutlierUpgradeComponent::IsNodeUnlocked(FName NodeIdOrRowName) const
{
	FName RowName = NAME_None;
	return ResolveNodeRowName(NodeIdOrRowName, RowName) && IsNodeUnlockedInternal(RowName);
}

bool UOutlierUpgradeComponent::HasUpgradeTag(FGameplayTag UpgradeTag) const
{
	return UpgradeTag.IsValid() && ActiveUpgradeTags.HasTagExact(UpgradeTag);
}

bool UOutlierUpgradeComponent::GetNodeRow(FName NodeIdOrRowName, FOutlierUpgradeNodeRow& OutNodeRow) const
{
	if (const FOutlierUpgradeNodeRow* Row = FindNodeRow(NodeIdOrRowName))
	{
		OutNodeRow = *Row;
		return true;
	}

	OutNodeRow = FOutlierUpgradeNodeRow();
	return false;
}

bool UOutlierUpgradeComponent::GetNodeViewData(FName NodeIdOrRowName, FOutlierUpgradeNodeViewData& OutViewData) const
{
	FName RowName = NAME_None;
	const FOutlierUpgradeNodeRow* Row = FindNodeRow(NodeIdOrRowName, &RowName);
	if (!Row)
	{
		OutViewData = FOutlierUpgradeNodeViewData();
		return false;
	}

	OutViewData.RowName = RowName;
	OutViewData.NodeRow = *Row;
	OutViewData.State = GetNodeState(RowName);
	OutViewData.CurrentNodeCount = GetCurrentNodeCount();
	OutViewData.bCanAfford = !bConsumeNodeCost || OutViewData.CurrentNodeCount >= Row->Cost;
	return true;
}

bool UOutlierUpgradeComponent::ResolveNodeRowNameByTreeAndNodeId(FName TreeId, FName NodeId, FName& OutRowName) const
{
	if (TreeId.IsNone() || NodeId.IsNone())
	{
		OutRowName = NAME_None;
		return false;
	}

	if (const FName* RowName = RowNamesByTreeNodeId.Find(BuildTreeNodeLookupKey(TreeId, NodeId)))
	{
		OutRowName = *RowName;
		return true;
	}

	OutRowName = NAME_None;
	return false;
}

int32 UOutlierUpgradeComponent::GetCurrentNodeCount() const
{
	const AOutlierPlayerState* PlayerState = GetOwningOutlierPlayerState();
	return PlayerState ? PlayerState->GetNodeCount() : 0;
}

void UOutlierUpgradeComponent::ServerTryActivateNode_Implementation(FName NodeIdOrRowName)
{
	ActivateNodeInternal(NodeIdOrRowName);
}

void UOutlierUpgradeComponent::OnRep_ActivatedNodeIds()
{
	RebuildRuntimeSets();
	RebuildUnlockedNodes();
	SyncActiveTagsToAbilityComponent();
	OnUpgradeStateChanged.Broadcast();
}

bool UOutlierUpgradeComponent::ActivateNodeInternal(FName NodeIdOrRowName, AOutlierPlayerState* InPlayerState)
{
	RefreshActivatedNodesFromPlayerState();
	RebuildRuntimeSets();

	FName RowName = NAME_None;
	const FOutlierUpgradeNodeRow* Row = FindNodeRow(NodeIdOrRowName, &RowName);
	if (!Row)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Upgrade] Activate failed: row not found Owner=%s Node=%s"),
			*GetNameSafe(GetOwner()),
			*NodeIdOrRowName.ToString());
		return false;
	}

	if (ActivatedNodeSet.Contains(RowName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Upgrade] Activate failed: already activated Owner=%s Row=%s"),
			*GetNameSafe(GetOwner()),
			*RowName.ToString());
		return false;
	}

	if (!IsNodeUnlockedInternal(RowName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Upgrade] Activate failed: locked Owner=%s Row=%s Parent=%s"),
			*GetNameSafe(GetOwner()),
			*RowName.ToString(),
			*Row->ParentId.ToString());
		return false;
	}

	if (!ConsumeNodeCost(Row->Cost, InPlayerState))
	{
		const AOutlierPlayerState* CostPlayerState = InPlayerState ? InPlayerState : GetOwningOutlierPlayerState();
		UE_LOG(LogTemp, Warning,
			TEXT("[Upgrade] Activate failed: insufficient node Owner=%s Row=%s Cost=%d PlayerState=%s NodeCount=%d"),
			*GetNameSafe(GetOwner()),
			*RowName.ToString(),
			Row->Cost,
			*GetNameSafe(CostPlayerState),
			CostPlayerState ? CostPlayerState->GetNodeCount() : 0);
		return false;
	}

	AOutlierPlayerState* PersistentPlayerState = InPlayerState ? InPlayerState : GetOwningOutlierPlayerState();
	if (PersistentPlayerState && PersistentPlayerState->HasAuthority())
	{
		PersistentPlayerState->AddActivatedUpgradeNode(Role, RowName);
		ActivatedNodeIds = PersistentPlayerState->GetActivatedUpgradeNodeIds(Role);
	}
	else
	{
		ActivatedNodeIds.AddUnique(RowName);
	}

	RebuildRuntimeSets();
	RebuildUnlockedNodes();
	SyncActiveTagsToAbilityComponent();

	OnUpgradeNodeActivated.Broadcast(RowName, Row->UpgradeTag);
	OnUpgradeStateChanged.Broadcast();

	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}

	return true;
}

bool UOutlierUpgradeComponent::IsNodeUnlockedInternal(FName RowName) const
{
	const FOutlierUpgradeNodeRow* Row = NodeRowsByRowName.Find(RowName);
	if (!Row)
	{
		return false;
	}

	if (ActivatedNodeSet.Contains(RowName))
	{
		return true;
	}

	if (Row->ParentId.IsNone())
	{
		return true;
	}

	FName ParentRowName = NAME_None;
	return ResolveNodeRowName(Row->ParentId, ParentRowName) && ActivatedNodeSet.Contains(ParentRowName);
}

bool UOutlierUpgradeComponent::ResolveNodeRowName(FName NodeIdOrRowName, FName& OutRowName) const
{
	if (NodeIdOrRowName.IsNone())
	{
		OutRowName = NAME_None;
		return false;
	}

	if (NodeRowsByRowName.Contains(NodeIdOrRowName))
	{
		OutRowName = NodeIdOrRowName;
		return true;
	}

	if (const FName* RowName = RowNamesByNodeId.Find(NodeIdOrRowName))
	{
		OutRowName = *RowName;
		return true;
	}

	OutRowName = NAME_None;
	return false;
}

const FOutlierUpgradeNodeRow* UOutlierUpgradeComponent::FindNodeRow(FName NodeIdOrRowName, FName* OutRowName) const
{
	FName RowName = NAME_None;
	if (!ResolveNodeRowName(NodeIdOrRowName, RowName))
	{
		return nullptr;
	}

	if (OutRowName)
	{
		*OutRowName = RowName;
	}

	return NodeRowsByRowName.Find(RowName);
}

FName UOutlierUpgradeComponent::BuildTreeNodeLookupKey(FName TreeId, FName NodeId)
{
	return FName(*FString::Printf(TEXT("%s::%s"), *TreeId.ToString(), *NodeId.ToString()));
}

void UOutlierUpgradeComponent::RebuildRuntimeSets()
{
	ActivatedNodeSet.Reset();
	ActiveUpgradeTags.Reset();

	for (const FName& NodeIdOrRowName : ActivatedNodeIds)
	{
		FName RowName = NAME_None;
		const FOutlierUpgradeNodeRow* Row = FindNodeRow(NodeIdOrRowName, &RowName);
		if (!Row)
		{
			continue;
		}

		ActivatedNodeSet.Add(RowName);
		if (Row->UpgradeTag.IsValid())
		{
			ActiveUpgradeTags.AddTag(Row->UpgradeTag);
		}
	}
}

void UOutlierUpgradeComponent::RebuildUnlockedNodes()
{
	UnlockedNodeIds.Reset();
	UnlockedNodeSet.Reset();

	for (const TPair<FName, FOutlierUpgradeNodeRow>& Pair : NodeRowsByRowName)
	{
		if (!ActivatedNodeSet.Contains(Pair.Key) && IsNodeUnlockedInternal(Pair.Key))
		{
			UnlockedNodeIds.Add(Pair.Key);
			UnlockedNodeSet.Add(Pair.Key);
		}
	}
}

void UOutlierUpgradeComponent::SyncActiveTagsToAbilityComponent() const
{
	// TODO(GAS): 커스텀 UOutlierAbilityComponent는 GAS 머지로 제거됨.
	// 업그레이드 태그를 ASC(UOutlierAbilitySystemComponent)로 투영하는 경로를 아직 정하지 않아 no-op 처리.
	// 설계 확정 시(loose 태그 vs 업그레이드 GE) 여기서 PlayerState 기준으로 ASC에 재적용할 것.
	// 현재 업그레이드는 ActiveUpgradeTags / HasUpgradeTag 로 내부 추적은 유지됨.
}

bool UOutlierUpgradeComponent::ConsumeNodeCost(int32 Cost, AOutlierPlayerState* InPlayerState) const
{
	if (!bConsumeNodeCost || Cost <= 0)
	{
		return true;
	}

	AOutlierPlayerState* PlayerState = InPlayerState ? InPlayerState : GetOwningOutlierPlayerState();
	return PlayerState && PlayerState->ConsumeNode(Cost);
}

AOutlierPlayerState* UOutlierUpgradeComponent::GetOwningOutlierPlayerState() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (AOutlierPlayerState* PlayerStateOwner = const_cast<AOutlierPlayerState*>(Cast<AOutlierPlayerState>(Owner)))
	{
		return PlayerStateOwner;
	}

	const APawn* PawnOwner = Cast<APawn>(Owner);
	if (PawnOwner)
	{
		if (AOutlierPlayerState* PawnPlayerState = PawnOwner->GetPlayerState<AOutlierPlayerState>())
		{
			return PawnPlayerState;
		}
	}

	const AController* Controller = PawnOwner ? PawnOwner->GetController() : nullptr;
	return Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
}
