#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Upgrade/OutlierUpgradeEffectTypes.h"
#include "GAS/Data/OutlierPartnerAbilityConfig.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "OutlierUpgradeComponent.generated.h"

class AOutlierPlayerState;
class UDataTable;
class UOutlierUpgradeSetData;
class UOutlierAbilitySystemComponent;
class UGameplayEffect;
struct FGameplayAttribute;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOutlierUpgradeStateChanged);
// 노드 활성화 브로드캐스트. 소비 측은 RowName 으로 DT_UpgradeEffect 효과를 조회한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOutlierUpgradeNodeActivated, FName, RowName);

UCLASS(ClassGroup = (Upgrade), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UOutlierUpgradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOutlierUpgradeComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeRole(EOutlierUpgradeRole InRole);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeDataTable(UDataTable* InDataTable);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeEffectDataTable(UDataTable* InEffectDataTable);

	// 노드 RowName 에 조인된 효과 배열을 반환한다 ( 없으면 빈 배열 ).
	// UFUNCTION 은 참조 반환을 지원하지 않으므로 순수 C++ 접근자로 둔다.
	const TArray<FOutlierUpgradeEffectRow>& GetNodeEffects(FName NodeIdOrRowName) const;

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SetUpgradeSetData(UOutlierUpgradeSetData* InUpgradeSetData);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RebuildNodeCache();

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void RefreshActivatedNodesFromPlayerState();

	// PS 의 활성 노드 목록을 기준으로 컴포넌트 런타임 상태(Activated/Unlocked/ActiveUpgradeTags)를
	// 전부 재계산하고, authority 라면 ASC 에 태그/Attribute/Config 를 재투영한다.
	// ASC 의 AbilityActorInfo 가 이제 막 준비된 시점( PossessedBy / OnRep_Controller 이후 )에 호출한다.
	// ( BeginPlay 시점엔 ASC 가 아직 없어 ReconcileUpgradeProjection 이 SKIP 되므로, 그 이후 재호출이 필요 )
	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SyncFromPlayerState();

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	bool TryActivateNode(FName NodeIdOrRowName);

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	bool TryActivateNodeForPlayerState(FName NodeIdOrRowName, AOutlierPlayerState* InPlayerState);

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool CanActivateNode(FName NodeIdOrRowName) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	EOutlierUpgradeNodeState GetNodeState(FName NodeIdOrRowName) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool IsNodeActivated(FName NodeIdOrRowName) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool IsNodeUnlocked(FName NodeIdOrRowName) const;

	// GrantAbility / FunctionOverride 로 활성화된 업그레이드 태그 질의. ActivatedNodeIds(리플리케이트됨) 로
	// 만든 캐시라 서버/클라 어디서 물어봐도 안전하다.
	// ( Attribute / AbilityConfig 타입 효과는 대상이 아니다 - 그 TargetTag 는 On/Off 신호가 아니라
	//   건드릴 수치의 주소일 뿐이라서. 그쪽은 IsNodeActivated() 로 노드 단위로 확인할 것. )
	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool HasUpgradeTag(FGameplayTag UpgradeTag) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	FGameplayTagContainer GetActiveUpgradeTags() const { return ActiveUpgradeTags; }

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool GetNodeRow(FName NodeIdOrRowName, FOutlierUpgradeNodeRow& OutNodeRow) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool GetNodeViewData(FName NodeIdOrRowName, FOutlierUpgradeNodeViewData& OutViewData) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	bool ResolveNodeRowNameByTreeAndNodeId(FName TreeId, FName NodeId, FName& OutRowName) const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	int32 GetCurrentNodeCount() const;

	UFUNCTION(BlueprintPure, Category = "Upgrade")
	UOutlierUpgradeSetData* GetUpgradeSetData() const { return UpgradeSetData; }

	// 활성 노드의 효과 전량을 ASC 에 재투영한다 ( 합성 GE 를 apply-new -> remove-old 로 스왑 ). authority 에서만 실제 투영.
	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void ReconcileUpgradeProjection();

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnOutlierUpgradeStateChanged OnUpgradeStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnOutlierUpgradeNodeActivated OnUpgradeNodeActivated;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UOutlierUpgradeSetData> UpgradeSetData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

	// 효과 테이블 ( FOutlierUpgradeEffectRow ). NodeRowName 으로 노드에 조인된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeEffectDataTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	EOutlierUpgradeRole Role = EOutlierUpgradeRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Upgrade")
	bool bConsumeNodeCost = true;

	UPROPERTY(ReplicatedUsing = OnRep_ActivatedNodeIds, VisibleInstanceOnly, BlueprintReadOnly, Category = "Upgrade")
	TArray<FName> ActivatedNodeIds;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Upgrade")
	TArray<FName> UnlockedNodeIds;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Upgrade")
	FGameplayTagContainer ActiveUpgradeTags;

	UFUNCTION(Server, Reliable)
	void ServerTryActivateNode(FName NodeIdOrRowName);

	UFUNCTION()
	void OnRep_ActivatedNodeIds();

private:
	bool ActivateNodeInternal(FName NodeIdOrRowName, AOutlierPlayerState* InPlayerState = nullptr);
	bool IsNodeUnlockedInternal(FName RowName) const;
	bool ResolveNodeRowName(FName NodeIdOrRowName, FName& OutRowName) const;
	const FOutlierUpgradeNodeRow* FindNodeRow(FName NodeIdOrRowName, FName* OutRowName = nullptr) const;
	static FName BuildTreeNodeLookupKey(FName TreeId, FName NodeId);

	void RebuildEffectCache();
	void RebuildRuntimeSets();
	void RebuildUnlockedNodes();
	bool ConsumeNodeCost(int32 Cost, AOutlierPlayerState* InPlayerState = nullptr) const;
	AOutlierPlayerState* GetOwningOutlierPlayerState() const;

	// ── ASC 투영 ──────────────────────────────────────────────
	UOutlierAbilitySystemComponent* GetOwningAbilitySystem() const;
	// Attribute 모디파이어 + GrantAbility/FunctionOverride 태그를 하나의 합성 GE 로 묶어서 투영한다.
	// ( 태그를 Loose Tag 로 따로 관리하지 않는 이유는 UpgradeAttributeEffectHandle 선언부 주석 참고 )
	void ProjectAttributes(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	void ProjectAbilityConfig(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	static FGameplayAttribute ResolveAttribute(const FGameplayTag& Tag);

	//실질적인 핵심 관리.
	TMap<FName, FOutlierUpgradeNodeRow> NodeRowsByRowName;
	//디자이너용 NodeID ->UI나 BP에서 NodeId만 넘겨돋 실제 Row name으로 해석하기 위한 look up (편의성)
	TMap<FName, FName> RowNamesByNodeId;
	//위랑 같이 쓰임, Nodeid 만 같이 쓰면 Tree 정보를 몰라서 정확한 row를 찾기 위해
	TMap<FName, FName> RowNamesByTreeNodeId;
	//NodeRowName -> 그 노드가 만드는 효과 배열 ( 효과 테이블 조인 결과 )
	TMap<FName, TArray<FOutlierUpgradeEffectRow>> EffectsByNodeRowName;

	//런타임 판정용 Set,
	TSet<FName> ActivatedNodeSet;
	TSet<FName> UnlockedNodeSet;

	// ── 투영 추적 ( flush 대상 ) ───────────────────────────────
	// 합성 GE 핸들 ( 매 리컨사일 1개 ). Attribute 모디파이어와 GrantAbility/FunctionOverride 태그를
	// 이 GE 하나에 같이 담아서 적용한다 ( apply-new -> remove-old 로 스왑 ).
	// Loose Tag 대신 이 방식을 쓰는 이유:
	// - 진짜 Active GameplayEffect 라서 리플리케이트된다 ( Loose Tag 는 서버 로컬에만 남고 클라로 안 감 ).
	// - GAS 가 "이 GE 가 어떤 태그를 몇 개 부여했는지"를 스스로 추적하므로, 두 노드가 같은 태그를
	//   공유해도 우리가 직접 카운트를 맞출 필요가 없다 ( 핸들 하나 제거하면 그 GE 가 준 만큼만
	//   정확히 빠진다 - Loose Tag 방식에서 있었던 leak 위험이 구조적으로 없다 ).
	//
	// 주의: ProjectAttributes 는 이 GE 를 절대 재사용/캐싱하면 안 되고 매 리컨사일마다 새로 만들어야
	// 한다. GrantedTags 의 Add/Remove 는 둘 다 "그 효과의 Def(=이 GE 오브젝트)가 지금 뭘 grant
	// 한다고 되어있나"를 그 순간 다시 읽어서 처리한다 ( Modifiers 처럼 Spec 생성 시점에 스냅샷되지
	// 않음 ). 그래서 같은 GE 오브젝트를 계속 고쳐 쓰면, 옛 활성 효과를 제거할 때 그 사이에 새로
	// 추가된 태그까지 같이 빠져버리는 leak 이 생긴다 ( 실제로 발생 확인함 - 17개 노드를 찍었는데
	// 태그가 1개만 남는 버그로 나타났었음 ).
	FActiveGameplayEffectHandle UpgradeAttributeEffectHandle;
	// AbilityConfig 재계산의 기준(base)은 여기서 캐시하지 않는다.
	// ASC 가 grant 시점에 고정해 들고 있는 GetBaseShooterSuitConfig / GetBasePartnerAbilityConfig 를
	// 매 리컨사일마다 읽는다 ( 두 벌로 나뉘면 재초기화·리스폰 때 어긋난다 ).
};
