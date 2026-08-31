#include "Upgrade/OutlierUpgradeComponent.h"

#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
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

		// FunctionOverride 효과의 태그를 활성 태그 컨테이너로 집계한다 ( HasUpgradeTag 질의용 ).
		if (const TArray<FOutlierUpgradeEffectRow>* Effects = EffectsByNodeRowName.Find(RowName))
		{
			for (const FOutlierUpgradeEffectRow& Effect : *Effects)
			{
				if (Effect.EffectType == EOutlierUpgradeEffectType::FunctionOverride && Effect.TargetTag.IsValid())
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
	}

	// TargetTag( Ability.Shooter.X ) 로 어떤 능력의 SuitConfig 하위 행인지 해석한다.
	FOutlierShooterSuitAbilityDataRow* ResolveSuitRow(FOutlierShooterSuitConfig& Config, const FGameplayTag& AbilityTag)
	{
		static const FGameplayTag QuantumLeap = FGameplayTag::RequestGameplayTag(TEXT("Ability.Shooter.QuantumLeap"), false);
		static const FGameplayTag BulletReflection = FGameplayTag::RequestGameplayTag(TEXT("Ability.Shooter.BulletReflection"), false);
		static const FGameplayTag Stealth = FGameplayTag::RequestGameplayTag(TEXT("Ability.Shooter.Stealth"), false);
		static const FGameplayTag WeaponOvercharge = FGameplayTag::RequestGameplayTag(TEXT("Ability.Shooter.WeaponOvercharge"), false);

		if (AbilityTag == QuantumLeap)      return &Config.QuantumLeap;
		if (AbilityTag == BulletReflection) return &Config.BulletReflection;
		if (AbilityTag == Stealth)          return &Config.Stealth;
		if (AbilityTag == WeaponOvercharge) return &Config.WeaponOvercharge;
		return nullptr;
	}

	// ConfigField 이름으로 SuitConfig 하위 행의 해당 필드에 델타를 적용한다.
	void ApplySuitConfigField(FOutlierShooterSuitAbilityDataRow& Row, FName Field, EOutlierUpgradeModOp Op, float Magnitude)
	{
		if (Field == TEXT("DurationSeconds"))            ApplyConfigDelta(Row.DurationSeconds, Op, Magnitude);
		else if (Field == TEXT("CooldownSeconds"))       ApplyConfigDelta(Row.CooldownSeconds, Op, Magnitude);
		else if (Field == TEXT("CastTimeSeconds"))       ApplyConfigDelta(Row.CastTimeSeconds, Op, Magnitude);
		else if (Field == TEXT("MaxPartnerDistance"))    ApplyConfigDelta(Row.MaxPartnerDistance, Op, Magnitude);
		else if (Field == TEXT("PartnerOffset"))         ApplyConfigDelta(Row.PartnerOffset, Op, Magnitude);
		else if (Field == TEXT("ReflectionRadius"))      ApplyConfigDelta(Row.ReflectionRadius, Op, Magnitude);
		else if (Field == TEXT("ShieldDrainPerSecond"))  ApplyConfigDelta(Row.ShieldDrainPerSecond, Op, Magnitude);
		else if (Field == TEXT("FireRateMultiplier"))    ApplyConfigDelta(Row.FireRateMultiplier, Op, Magnitude);
		else if (Field == TEXT("SpreadMultiplier"))      ApplyConfigDelta(Row.SpreadMultiplier, Op, Magnitude);
		else if (Field == TEXT("ShieldRecoveryDelay"))   ApplyConfigDelta(Row.ShieldRecoveryDelay, Op, Magnitude);
		// 미매칭 필드( 예: DecoyDuration )는 아직 SuitConfig 에 없어 무시된다.
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

	// (1) 지난 투영분 flush ( 우리가 붙인 것만 )
	FlushUpgradeProjection(ASC);

	// (2) 활성 노드의 효과 전량 수집
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

	// (3) 타입별 투영
	ProjectAttributes(ASC, Effects);
	ProjectAbilityConfig(ASC, Effects);
	ProjectGrantAndOverrideTags(ASC, Effects);
	ProjectPassiveEffects(ASC, Effects);

	// (4) 검증용 요약 로그
	LogUpgradeProjectionState(ASC);
}

void UOutlierUpgradeComponent::DumpUpgradeProjectionState() const
{
	UOutlierAbilitySystemComponent* ASC = GetOwningAbilitySystem();
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Projection] DumpState: ASC 없음 Owner=%s"), *GetNameSafe(GetOwner()));
		return;
	}
	LogUpgradeProjectionState(ASC);
}

void UOutlierUpgradeComponent::DumpUpgradePassiveEffects() const
{
	UOutlierAbilitySystemComponent* ASC = GetOwningAbilitySystem();
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Passive] ASC 없음 Owner=%s"), *GetNameSafe(GetOwner()));
		return;
	}

	const float Shield = ASC->GetNumericAttribute(UOutlierShieldAttributeSet::GetShieldAttribute());
	const float MaxShield = ASC->GetNumericAttribute(UOutlierShieldAttributeSet::GetMaxShieldAttribute());
	const bool bReflecting = ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.BulletReflecting"), false));
	const bool bStealthed = ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.Stealthed"), false));

	UE_LOG(LogTemp, Log, TEXT("[Upgrade.Passive] ===== Owner=%s PassiveGE=%d Shield=%.1f/%.1f gate[Reflecting=%d Stealthed=%d] ====="),
		*GetNameSafe(GetOwner()), UpgradePassiveEffectHandles.Num(), Shield, MaxShield, bReflecting, bStealthed);

	for (const FActiveGameplayEffectHandle& Handle : UpgradePassiveEffectHandles)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		if (!Active)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Passive]   (handle 무효/제거됨)"));
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("[Upgrade.Passive]   GE=%s inhibited=%d period=%.2f duration=%.1f"),
			*GetNameSafe(Active->Spec.Def),
			Active->bIsInhibited ? 1 : 0,
			Active->Spec.GetPeriod(),
			Active->Spec.GetDuration());
	}
}

void UOutlierUpgradeComponent::LogUpgradeProjectionState(UOutlierAbilitySystemComponent* ASC) const
{
	if (!ASC)
	{
		return;
	}

	const float MaxShield = ASC->GetNumericAttribute(UOutlierShieldAttributeSet::GetMaxShieldAttribute());
	const float MaxHealth = ASC->GetNumericAttribute(UOutlierVitalAttributeSet::GetMaxHealthAttribute());
	const FOutlierShooterSuitConfig& Cfg = ASC->GetShooterSuitConfig();

	UE_LOG(LogTemp, Log, TEXT("[Upgrade.Projection] ===== Owner=%s Role=%d ActivatedNodes=%d ====="),
		*GetNameSafe(GetOwner()), static_cast<int32>(Role), ActivatedNodeSet.Num());

	// 활성 노드별 효과 나열
	for (const FName& RowName : ActivatedNodeSet)
	{
		if (const TArray<FOutlierUpgradeEffectRow>* Rows = EffectsByNodeRowName.Find(RowName))
		{
			for (const FOutlierUpgradeEffectRow& E : *Rows)
			{
				UE_LOG(LogTemp, Log, TEXT("[Upgrade.Projection]   %s | type=%d target=%s field=%s op=%d mag=%.2f"),
					*RowName.ToString(), static_cast<int32>(E.EffectType),
					*E.TargetTag.ToString(), *E.ConfigField.ToString(),
					static_cast<int32>(E.Op), E.Magnitude);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Upgrade.Projection]   Attr: MaxShield=%.1f MaxHealth=%.1f (AttrGE=%s)"),
		MaxShield, MaxHealth, UpgradeAttributeEffectHandle.IsValid() ? TEXT("on") : TEXT("off"));
	UE_LOG(LogTemp, Log, TEXT("[Upgrade.Projection]   Config: Reflect(cd=%.1f dur=%.1f) Leap(cd=%.1f range=%.0f) Stealth(cd=%.1f dur=%.1f) Overcharge(cd=%.1f dur=%.1f drain=%.1f fire=%.2f)"),
		Cfg.BulletReflection.CooldownSeconds, Cfg.BulletReflection.DurationSeconds,
		Cfg.QuantumLeap.CooldownSeconds, Cfg.QuantumLeap.MaxPartnerDistance,
		Cfg.Stealth.CooldownSeconds, Cfg.Stealth.DurationSeconds,
		Cfg.WeaponOvercharge.CooldownSeconds, Cfg.WeaponOvercharge.DurationSeconds,
		Cfg.WeaponOvercharge.ShieldDrainPerSecond, Cfg.WeaponOvercharge.FireRateMultiplier);
	UE_LOG(LogTemp, Log, TEXT("[Upgrade.Projection]   Tags=[%s] PassiveGE=%d"),
		*AppliedProjectionTags.ToStringSimple(), UpgradePassiveEffectHandles.Num());
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

void UOutlierUpgradeComponent::FlushUpgradeProjection(UOutlierAbilitySystemComponent* ASC)
{
	// 참고: Attribute 집계 GE 는 여기서 제거하지 않는다.
	// ProjectAttributes 가 "새 GE 적용 -> 옛 GE 제거" 순서로 자체 관리해 MaxShield dip 을 막는다.
	for (FActiveGameplayEffectHandle& Handle : UpgradePassiveEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	UpgradePassiveEffectHandles.Reset();

	for (const FGameplayTag& Tag : AppliedProjectionTags)
	{
		ASC->RemoveLooseGameplayTag(Tag);
	}
	AppliedProjectionTags.Reset();
}

void UOutlierUpgradeComponent::ProjectAttributes(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects)
{
	UGameplayEffect* GE = NewObject<UGameplayEffect>(GetTransientPackage());
	GE->DurationPolicy = EGameplayEffectDurationType::Infinite;

	for (const FOutlierUpgradeEffectRow* Effect : Effects)
	{
		if (Effect->EffectType != EOutlierUpgradeEffectType::Attribute)
		{
			continue;
		}

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

	// 새 집계 GE 를 먼저 적용한 뒤 이전 GE 를 제거한다.
	// ( 먼저 제거하면 MaxShield 가 순간 base 로 떨어져 현재 Shield 가 clamp 되는 dip 발생 )
	FActiveGameplayEffectHandle NewHandle;
	if (GE->Modifiers.Num() > 0)
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
	// 현재 config 투영은 Shooter suit 기준. Partner suit config 투영은 별도 설계 예정.
	if (Role != EOutlierUpgradeRole::Shooter)
	{
		return;
	}

	if (!bBaseSuitConfigCaptured)
	{
		// suit init( ConfigureShooterSuitAbilities ) 전에는 base 가 준비 안 됨.
		// 준비될 때까지 포착을 미룬다 ( 다음 리컨사일에 다시 시도 ).
		if (!ASC->IsShooterSuitConfigured())
		{
			return;
		}
		// 업그레이드 이전 base config 를 최초 1회 포착 ( init 에서 DT_AbilityShooter 로 세팅된 값 ).
		BaseSuitConfig = ASC->GetShooterSuitConfig();
		bBaseSuitConfigCaptured = true;
	}

	FOutlierShooterSuitConfig Config = BaseSuitConfig;
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
}

void UOutlierUpgradeComponent::ProjectGrantAndOverrideTags(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects)
{
	for (const FOutlierUpgradeEffectRow* Effect : Effects)
	{
		const bool bIsTagEffect =
			Effect->EffectType == EOutlierUpgradeEffectType::GrantAbility ||
			Effect->EffectType == EOutlierUpgradeEffectType::FunctionOverride;
		if (!bIsTagEffect || !Effect->TargetTag.IsValid())
		{
			continue;
		}

		ASC->AddLooseGameplayTag(Effect->TargetTag);
		AppliedProjectionTags.AddTag(Effect->TargetTag);
	}
}

void UOutlierUpgradeComponent::ProjectPassiveEffects(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects)
{
	for (const FOutlierUpgradeEffectRow* Effect : Effects)
	{
		if (Effect->EffectType != EOutlierUpgradeEffectType::ApplyEffect)
		{
			continue;
		}

		// GE 클래스는 CSV 경로가 아니라 DataAsset 의 ApplyEffectClasses 에서 키로 조회한다.
		TSubclassOf<UGameplayEffect> GEClass = nullptr;
		if (UpgradeSetData && !Effect->EffectClassKey.IsNone())
		{
			GEClass = UpgradeSetData->ApplyEffectClasses.FindRef(Effect->EffectClassKey);
		}
		if (!GEClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Projection] ApplyEffect 키 미해결: key=%s ( DataAsset ApplyEffectClasses 확인 )"),
				*Effect->EffectClassKey.ToString());
			continue;
		}

		const FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(GEClass, 1.0f, Context);
		if (Spec.IsValid())
		{
			UpgradePassiveEffectHandles.Add(ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()));
		}
	}
}

FGameplayAttribute UOutlierUpgradeComponent::ResolveAttribute(const FGameplayTag& Tag)
{
	// 테스트 단계: Shield / Health 만 지원. 이후 Weapon.Recoil 등 AttributeSet 신설 시 확장.
	static const FGameplayTag ShieldMax = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Shield.Max"), false);
	static const FGameplayTag HealthMax = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health.Max"), false);

	if (Tag == ShieldMax) return UOutlierShieldAttributeSet::GetMaxShieldAttribute();
	if (Tag == HealthMax) return UOutlierVitalAttributeSet::GetMaxHealthAttribute();
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
