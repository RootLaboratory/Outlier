#include "Upgrade/OutlierUpgradeComponent.h"

#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "Upgrade/OutlierUpgradeProjectionSettings.h"
#include "UObject/UnrealType.h"

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
		UpgradeEffectDataTable = UpgradeSetData->UpgradeEffectDataTable;
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

void UOutlierUpgradeComponent::SetUpgradeEffectDataTable(UDataTable* InEffectDataTable)
{
	if (UpgradeEffectDataTable == InEffectDataTable)
	{
		return;
	}

	UpgradeEffectDataTable = InEffectDataTable;
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
		UpgradeEffectDataTable = UpgradeSetData->UpgradeEffectDataTable;
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

	RebuildEffectCache();
	RefreshActivatedNodesFromPlayerState();
	RebuildRuntimeSets();
	RebuildUnlockedNodes();
	ReconcileUpgradeProjection();
	OnUpgradeStateChanged.Broadcast();
}

void UOutlierUpgradeComponent::RebuildEffectCache()
{
	EffectsByNodeRowName.Reset();

	if (!UpgradeEffectDataTable)
	{
		return;
	}

	for (const TPair<FName, uint8*>& Pair : UpgradeEffectDataTable->GetRowMap())
	{
		const FOutlierUpgradeEffectRow* EffectRow = reinterpret_cast<const FOutlierUpgradeEffectRow*>(Pair.Value);
		if (!EffectRow || EffectRow->NodeRowName.IsNone())
		{
			continue;
		}

		// 노드 테이블에 존재하는 ( 그리고 현재 Role 로 필터된 ) 노드에만 조인한다.
		if (!NodeRowsByRowName.Contains(EffectRow->NodeRowName))
		{
			continue;
		}

		EffectsByNodeRowName.FindOrAdd(EffectRow->NodeRowName).Add(*EffectRow);
	}
}

const TArray<FOutlierUpgradeEffectRow>& UOutlierUpgradeComponent::GetNodeEffects(FName NodeIdOrRowName) const
{
	static const TArray<FOutlierUpgradeEffectRow> EmptyEffects;

	FName RowName = NAME_None;
	if (!ResolveNodeRowName(NodeIdOrRowName, RowName))
	{
		return EmptyEffects;
	}

	const TArray<FOutlierUpgradeEffectRow>* Effects = EffectsByNodeRowName.Find(RowName);
	return Effects ? *Effects : EmptyEffects;
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

void UOutlierUpgradeComponent::SyncFromPlayerState()
{
	// (1) PS 의 ActivatedNodeIds 를 다시 읽어온다 ( 세이브 로드 / 리스폰 등으로 PS 쪽이 먼저 갱신됐을 수 있음 ).
	RefreshActivatedNodesFromPlayerState();

	// (2) Activated/Unlocked 런타임 Set 및 ActiveUpgradeTags 캐시를 재계산한다.
	RebuildRuntimeSets();
	RebuildUnlockedNodes();

	// (3) authority 라면 ASC 에 Attribute / AbilityConfig / GrantAbility·FunctionOverride 태그를 재투영한다.
	//     ( non-authority 에서는 ReconcileUpgradeProjection 내부에서 조용히 no-op )
	ReconcileUpgradeProjection();

	OnUpgradeStateChanged.Broadcast();
}

void UOutlierUpgradeComponent::ServerTryActivateNode_Implementation(FName NodeIdOrRowName)
{
	ActivateNodeInternal(NodeIdOrRowName);
}

void UOutlierUpgradeComponent::OnRep_ActivatedNodeIds()
{
	RebuildRuntimeSets();
	RebuildUnlockedNodes();
	ReconcileUpgradeProjection();
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
	ReconcileUpgradeProjection();

	OnUpgradeNodeActivated.Broadcast(RowName);
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

		// GrantAbility / FunctionOverride 효과의 태그를 활성 태그 컨테이너로 집계한다 ( HasUpgradeTag 질의용 ).
		// 이 둘은 TargetTag 가 "이 기능이 켜져 있다"는 On/Off 신호라 여기 모을 수 있다.
		// ( Attribute/AbilityConfig 는 TargetTag 가 수치를 가리키는 주소일 뿐이라 대상이 아님 -
		//   그쪽은 IsNodeActivated() 로 노드 단위로 물어보거나 Attribute 값 자체를 읽어야 한다. )
		if (const TArray<FOutlierUpgradeEffectRow>* Effects = EffectsByNodeRowName.Find(RowName))
		{
			for (const FOutlierUpgradeEffectRow& Effect : *Effects)
			{
				const bool bIsTagEffect =
					Effect.EffectType == EOutlierUpgradeEffectType::GrantAbility ||
					Effect.EffectType == EOutlierUpgradeEffectType::FunctionOverride;
				if (bIsTagEffect && Effect.TargetTag.IsValid())
				{
					ActiveUpgradeTags.AddTag(Effect.TargetTag);
				}
			}
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

namespace
{
	EGameplayModOp::Type ToModifierOp(EOutlierUpgradeModOp Op)
	{
		switch (Op)
		{
		case EOutlierUpgradeModOp::Multiplicative: return EGameplayModOp::Multiplicitive;
		case EOutlierUpgradeModOp::Override:        return EGameplayModOp::Override;
		default:                                    return EGameplayModOp::Additive;
		}
	}

	// AbilityConfig 델타를 하나의 SuitConfig 필드에 적용한다.
	void ApplyConfigDelta(float& Field, EOutlierUpgradeModOp Op, float Magnitude)
	{
		switch (Op)
		{
		case EOutlierUpgradeModOp::Multiplicative: Field *= Magnitude; break;
		case EOutlierUpgradeModOp::Override:       Field = Magnitude;  break;
		default:                                   Field += Magnitude; break;
		}
		// 밸런스 델타가 과해서 쿨다운/사거리/양 등이 음수로 떨어지는 걸 막는다.
		// IsValid() 가 0 은 허용하므로, 여기서 0 하한을 걸어두면 과도한 감소 델타가
		// 리컨사일 전체를 거부시키는 assert/silent-fail 대신 0 으로 클램프되어 흡수된다.
		Field = FMath::Max(Field, 0.0f);
	}

	// TargetTag( Ability.Shooter.X ) 로 어떤 능력의 SuitConfig 하위 행인지 해석한다.
	// 매핑 테이블은 하드코딩이 아니라 UOutlierUpgradeProjectionSettings( ini ) 에서 읽고,
	// 실제 필드 포인터는 FStructProperty 리플렉션으로 찾는다 ( SuitConfigFieldName 은
	// FOutlierShooterSuitConfig 의 UPROPERTY 멤버명과 일치해야 한다 ).
	FOutlierShooterSuitAbilityDataRow* ResolveSuitRow(FOutlierShooterSuitConfig& Config, const FGameplayTag& AbilityTag)
	{
		if (!AbilityTag.IsValid())
		{
			return nullptr;
		}

		const UOutlierUpgradeProjectionSettings* Settings = GetDefault<UOutlierUpgradeProjectionSettings>();
		for (const FOutlierUpgradeSuitRoleMapping& Mapping : Settings->SuitRoleMappings)
		{
			if (Mapping.AbilityTag != AbilityTag || Mapping.SuitConfigFieldName.IsNone())
			{
				continue;
			}

			FStructProperty* StructProp = FindFProperty<FStructProperty>(
				FOutlierShooterSuitConfig::StaticStruct(), Mapping.SuitConfigFieldName);
			if (!StructProp)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Upgrade] SuitRoleMappings: '%s' 는 FOutlierShooterSuitConfig 에 없는 필드입니다 ( ini 오타 의심 )."),
					*Mapping.SuitConfigFieldName.ToString());
				continue;
			}
			return StructProp->ContainerPtrToValuePtr<FOutlierShooterSuitAbilityDataRow>(&Config);
		}
		return nullptr;
	}

	// ConfigField 이름으로 SuitConfig 하위 행의 해당 필드에 델타를 적용한다.
	// FOutlierShooterSuitAbilityDataRow 의 UPROPERTY 멤버명과 CSV ConfigField 가 1:1 컨벤션이므로
	// ( 헤더 주석 참고 ) 별도 매핑 테이블 없이 FFloatProperty 리플렉션으로 직접 찾는다.
	void ApplySuitConfigField(FOutlierShooterSuitAbilityDataRow& Row, FName Field, EOutlierUpgradeModOp Op, float Magnitude)
	{
		if (Field.IsNone())
		{
			return;
		}

		FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(
			FOutlierShooterSuitAbilityDataRow::StaticStruct(), Field);
		if (!FloatProp)
		{
			// 미매칭 필드( 예: DecoyDuration )는 아직 SuitConfig 에 없어 무시된다.
			return;
		}

		float* ValuePtr = FloatProp->ContainerPtrToValuePtr<float>(&Row);
		ApplyConfigDelta(*ValuePtr, Op, Magnitude);
	}

	// Partner 버전: FOutlierPartnerAbilityConfig 는 능력별 서브 row 로 나뉘지 않은 flat struct 라
	// ResolveSuitRow 같은 TargetTag -> 서브구조체 해석 단계 없이, ConfigField 이름으로
	// 최상위 필드를 바로 찾아 델타를 적용한다 ( TargetTag 는 CSV 가독성/문서화 용도로만 남는다 ).
	void ApplyPartnerConfigField(FOutlierPartnerAbilityConfig& Config, FName Field, EOutlierUpgradeModOp Op, float Magnitude)
	{
		if (Field.IsNone())
		{
			return;
		}

		FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(
			FOutlierPartnerAbilityConfig::StaticStruct(), Field);
		if (!FloatProp)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Upgrade] ConfigField '%s' 는 FOutlierPartnerAbilityConfig 에 없는 필드입니다 ( CSV 오타 의심 )."),
				*Field.ToString());
			return;
		}

		float* ValuePtr = FloatProp->ContainerPtrToValuePtr<float>(&Config);
		ApplyConfigDelta(*ValuePtr, Op, Magnitude);
	}
}

void UOutlierUpgradeComponent::ReconcileUpgradeProjection()
{
	// CDO / 아키타입 컴포넌트는 투영 대상이 아니다.
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 판정/능력은 서버에서만. 클라 OnRep 은 내부 UI 셋( ActiveUpgradeTags )만 이미 갱신됨.
		return;
	}

	UOutlierAbilitySystemComponent* ASC = GetOwningAbilitySystem();
	if (!ASC || !ASC->AbilityActorInfo.IsValid() || !ASC->AbilityActorInfo->AvatarActor.IsValid())
	{
		// ASC 가 아직 초기화되지 않음( AbilityActorInfo 없음 ).
		// MakeEffectContext / ApplyGameplayEffect 등이 ensure 실패 후 크래시하므로 스킵한다.
		// 활성화 상태는 PlayerState 에 남으므로 ASC 준비 후 리컨사일에서 재적용된다.
		UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Projection] SKIP: ASC 미준비 Owner=%s ASC=%s"),
			*GetNameSafe(GetOwner()), ASC ? TEXT("valid(no ActorInfo)") : TEXT("null"));
		return;
	}

	// (1) 활성 노드의 효과 전량 수집
	TArray<const FOutlierUpgradeEffectRow*> Effects;
	for (const FName& RowName : ActivatedNodeSet)
	{
		if (const TArray<FOutlierUpgradeEffectRow>* Rows = EffectsByNodeRowName.Find(RowName))
		{
			for (const FOutlierUpgradeEffectRow& Effect : *Rows)
			{
				Effects.Add(&Effect);
			}
		}
	}

	// (2) 타입별 투영 ( ProjectAttributes 가 Attribute 모디파이어 + GrantAbility/FunctionOverride 태그를
	//     하나의 합성 GE 로 같이 apply-new -> remove-old 스왑한다 - 별도 flush 단계가 필요 없다 )
	ProjectAttributes(ASC, Effects);
	ProjectAbilityConfig(ASC, Effects);
}

UOutlierAbilitySystemComponent* UOutlierUpgradeComponent::GetOwningAbilitySystem() const
{
	AActor* AvatarOwner = nullptr;
	if (const AOutlierPlayerState* PlayerState = GetOwningOutlierPlayerState())
	{
		AvatarOwner = PlayerState->GetPawn();
	}
	if (!AvatarOwner)
	{
		AvatarOwner = Cast<APawn>(GetOwner());
	}
	if (!AvatarOwner)
	{
		return nullptr;
	}

	return Cast<UOutlierAbilitySystemComponent>(
		UAbilitySystemGlobals::Get().GetAbilitySystemComponentFromActor(AvatarOwner));
}


void UOutlierUpgradeComponent::ProjectAttributes(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects)
{
	// 리컨사일마다 새 GE 오브젝트를 만든다 ( 절대 재사용/캐싱하지 않는다 ).
	// GrantedTags 의 Add/Remove 는 "그 효과의 Def(=이 GE 오브젝트)가 지금 뭘 grant 한다고
	// 되어있나"를 그 순간 다시 읽어서 처리하기 때문에, 같은 오브젝트를 계속 고쳐 쓰면 옛 활성
	// 효과를 제거할 때 그 사이에 새로 추가된 태그까지 같이 빠지는 leak 이 생긴다
	// ( UpgradeAttributeEffectHandle 선언부 주석 참고 - 실제로 겪은 버그 ).
	// 리컨사일이 매 틱이 아니라 노드 활성화/OnRep 시에만 도는 빈도라 GC 부담은 미미하다.
	UGameplayEffect* GE = NewObject<UGameplayEffect>(this);
	GE->DurationPolicy = EGameplayEffectDurationType::Infinite;

	// GrantAbility / FunctionOverride 태그도 이 GE 하나에 같이 담는다 ( Loose Tag 대신 -
	// UpgradeAttributeEffectHandle 선언부 주석 참고: 복제도 되고, 카운트도 GAS 가 알아서 관리한다 ).
	FGameplayTagContainer DesiredTags;

	for (const FOutlierUpgradeEffectRow* Effect : Effects)
	{
		if (Effect->EffectType == EOutlierUpgradeEffectType::Attribute)
		{
			const FGameplayAttribute Attribute = ResolveAttribute(Effect->TargetTag);
			if (!Attribute.IsValid())
			{
				continue;
			}

			const int32 Index = GE->Modifiers.AddDefaulted();
			FGameplayModifierInfo& Modifier = GE->Modifiers[Index];
			Modifier.Attribute = Attribute;
			Modifier.ModifierOp = ToModifierOp(Effect->Op);
			Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Effect->Magnitude));
		}
		else if (Effect->EffectType == EOutlierUpgradeEffectType::GrantAbility ||
			Effect->EffectType == EOutlierUpgradeEffectType::FunctionOverride)
		{
			if (Effect->TargetTag.IsValid())
			{
				DesiredTags.AddTag(Effect->TargetTag);
			}
		}
	}

	UTargetTagsGameplayEffectComponent& TagComponent = GE->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
	FInheritedTagContainer TagChanges;
	TagChanges.Added = DesiredTags;
	TagComponent.SetAndApplyTargetTagChanges(TagChanges);

	// 새 집계 GE 를 먼저 적용한 뒤 이전 GE 를 제거한다.
	// ( 먼저 제거하면 MaxShield 가 순간 base 로 떨어져 현재 Shield 가 clamp 되는 dip 발생.
	//   태그도 같은 GE 에 실려있어 이 순서 그대로 swap 되면 끊김 없이 넘어간다. )
	FActiveGameplayEffectHandle NewHandle;
	if (GE->Modifiers.Num() > 0 || !DesiredTags.IsEmpty())
	{
		NewHandle = ASC->ApplyGameplayEffectToSelf(GE, 1.0f, ASC->MakeEffectContext());
	}
	if (UpgradeAttributeEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(UpgradeAttributeEffectHandle);
	}
	UpgradeAttributeEffectHandle = NewHandle;
}

void UOutlierUpgradeComponent::ProjectAbilityConfig(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects)
{
	if (Role == EOutlierUpgradeRole::Shooter)
	{
		// suit init( ConfigureShooterSuitAbilities ) 전에는 base 가 없다. 다음 리컨사일에 다시 시도.
		if (!ASC->IsShooterSuitConfigured())
		{
			return;
		}

		// base 는 ASC 가 grant 시점에 고정해 들고 있다. 여기서 따로 캐시하면 두 벌이 되어
		// 재초기화/리스폰 때 어긋나므로 매번 ASC 에서 읽는다.
		FOutlierShooterSuitConfig Config = ASC->GetBaseShooterSuitConfig();
		for (const FOutlierUpgradeEffectRow* Effect : Effects)
		{
			if (Effect->EffectType != EOutlierUpgradeEffectType::AbilityConfig)
			{
				continue;
			}

			if (FOutlierShooterSuitAbilityDataRow* Row = ResolveSuitRow(Config, Effect->TargetTag))
			{
				ApplySuitConfigField(*Row, Effect->ConfigField, Effect->Op, Effect->Magnitude);
			}
		}

		// 활성 config 업그레이드가 없어도 base 로 되돌리기 위해 항상 푸시한다 ( 리컨사일 = 순수 함수 ).
		ASC->UpdateShooterSuitConfig(Config);
		return;
	}

	if (Role == EOutlierUpgradeRole::Partner)
	{
		// ConfigurePartnerAbilities( DT_Partner_Skill ) 전에는 base 가 없다. 다음 리컨사일에 다시 시도.
		if (!ASC->IsPartnerAbilitiesConfigured())
		{
			return;
		}

		FOutlierPartnerAbilityConfig Config = ASC->GetBasePartnerAbilityConfig();
		for (const FOutlierUpgradeEffectRow* Effect : Effects)
		{
			if (Effect->EffectType != EOutlierUpgradeEffectType::AbilityConfig)
			{
				continue;
			}

			// Partner 는 서브 row 가 없는 flat struct 라 TargetTag 로 서브구조체를 찾을 필요 없이
			// ConfigField 이름으로 바로 필드를 찾는다.
			ApplyPartnerConfigField(Config, Effect->ConfigField, Effect->Op, Effect->Magnitude);
		}

		ASC->UpdatePartnerAbilityConfig(Config);
		return;
	}

	// 그 외 Role( None 등 )은 AbilityConfig 투영 대상이 아니다.
}

FGameplayAttribute UOutlierUpgradeComponent::ResolveAttribute(const FGameplayTag& Tag)
{
	// AttributeMappings( ini, UOutlierUpgradeProjectionSettings ) 에서 Tag -> (AttributeSetClass, AttributeName)
	// 을 찾아 리플렉션으로 FGameplayAttribute 를 만든다. 새 Attribute 추가 시 C++ 수정 없이
	// Project Settings > Outlier Upgrade Projection 에 행만 추가하면 된다.
	if (!Tag.IsValid())
	{
		return FGameplayAttribute();
	}

	const UOutlierUpgradeProjectionSettings* Settings = GetDefault<UOutlierUpgradeProjectionSettings>();
	for (const FOutlierUpgradeAttributeMapping& Mapping : Settings->AttributeMappings)
	{
		if (Mapping.Tag != Tag || !Mapping.AttributeSetClass || Mapping.AttributeName.IsNone())
		{
			continue;
		}

		FProperty* Prop = FindFProperty<FProperty>(Mapping.AttributeSetClass, Mapping.AttributeName);
		if (!Prop)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Upgrade] AttributeMappings: %s 에 '%s' 프로퍼티가 없습니다 ( ini 오타 의심 )."),
				*Mapping.AttributeSetClass->GetName(), *Mapping.AttributeName.ToString());
			continue;
		}
		return FGameplayAttribute(Prop);
	}
	return FGameplayAttribute();
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
