#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Upgrade/OutlierUpgradeEffectTypes.h"
#include "GAS/Data/OutlierShooterSuitAbilityDataRow.h"
#include "OutlierUpgradeComponent.generated.h"

class AOutlierPlayerState;
class UDataTable;
class UOutlierUpgradeSetData;
class UOutlierAbilitySystemComponent;
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

	// 활성 노드의 효과 전량을 ASC 에 재투영한다 ( flush 후 재적용 ). authority 에서만 실제 투영.
	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void ReconcileUpgradeProjection();

	// 현재 투영 상태( 활성 노드 / attribute / config / 태그 )를 로그로 덤프한다 ( 검증용 ).
	UFUNCTION(BlueprintCallable, Category = "Upgrade|Debug")
	void DumpUpgradeProjectionState() const;

	// ApplyEffect 로 붙인 패시브 GE 들의 클래스 / 억제(inhibited) 상태 / 게이트 태그 / 실드값을 덤프한다.
	// 반사 중에 호출하면 inhibited=0( 깨어남 ) + Shield 상승, 평상시엔 inhibited=1( 잠듦 )로 게이팅 확인.
	UFUNCTION(BlueprintCallable, Category = "Upgrade|Debug")
	void DumpUpgradePassiveEffects() const;

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
	void FlushUpgradeProjection(UOutlierAbilitySystemComponent* ASC);
	void ProjectAttributes(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	void ProjectAbilityConfig(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	void ProjectGrantAndOverrideTags(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	void ProjectPassiveEffects(UOutlierAbilitySystemComponent* ASC, const TArray<const FOutlierUpgradeEffectRow*>& Effects);
	static FGameplayAttribute ResolveAttribute(const FGameplayTag& Tag);
	void LogUpgradeProjectionState(UOutlierAbilitySystemComponent* ASC) const;

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
	// 합성 Attribute GE 핸들 ( 매 리컨사일 1개 )
	FActiveGameplayEffectHandle UpgradeAttributeEffectHandle;
	// ApplyEffect 로 붙인 패시브 GE 핸들들
	TArray<FActiveGameplayEffectHandle> UpgradePassiveEffectHandles;
	// GrantAbility / FunctionOverride 로 부여한 loose 태그들
	FGameplayTagContainer AppliedProjectionTags;
	// AbilityConfig 재계산의 기준 ( 업그레이드 전 base, 최초 리컨사일에 1회 포착 )
	FOutlierShooterSuitConfig BaseSuitConfig;
	bool bBaseSuitConfigCaptured = false;
};
