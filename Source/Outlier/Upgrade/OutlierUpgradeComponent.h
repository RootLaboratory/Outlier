#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "OutlierUpgradeComponent.generated.h"

class AOutlierPlayerState;
class UDataTable;
class UOutlierUpgradeSetData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOutlierUpgradeStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOutlierUpgradeNodeActivated, FName, RowName, FGameplayTag, UpgradeTag);

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

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	void SyncActiveTagsToAbilityComponent() const;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnOutlierUpgradeStateChanged OnUpgradeStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Upgrade")
	FOnOutlierUpgradeNodeActivated OnUpgradeNodeActivated;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UOutlierUpgradeSetData> UpgradeSetData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrade")
	TObjectPtr<UDataTable> UpgradeDataTable;

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

	void RebuildRuntimeSets();
	void RebuildUnlockedNodes();
	bool ConsumeNodeCost(int32 Cost, AOutlierPlayerState* InPlayerState = nullptr) const;
	AOutlierPlayerState* GetOwningOutlierPlayerState() const;

	//실질적인 핵심 관리.
	TMap<FName, FOutlierUpgradeNodeRow> NodeRowsByRowName;
	//디자이너용 NodeID ->UI나 BP에서 NodeId만 넘겨돋 실제 Row name으로 해석하기 위한 look up (편의성)
	TMap<FName, FName> RowNamesByNodeId;
	//위랑 같이 쓰임, Nodeid 만 같이 쓰면 Tree 정보를 몰라서 정확한 row를 찾기 위해
	TMap<FName, FName> RowNamesByTreeNodeId;

	//런타임 판정용 Set, 
	TSet<FName> ActivatedNodeSet;
	TSet<FName> UnlockedNodeSet;
};
