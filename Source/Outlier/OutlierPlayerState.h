// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Save/OutlierCheckpointData.h"
#include "Save/OutlierLoadoutSnapshot.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "OutlierPlayerState.generated.h"

class AShooterCharacter;
class USkeletalMesh;
class APartnerCharacter;
class AOutlierPlayerState;

UENUM(BlueprintType)
enum class EOutlierPlayerRole : uint8
{
	None,
	Shooter,
	Partner
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerRoleChanged, AOutlierPlayerState*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPendingLobbyStateChanged, AOutlierPlayerState*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerCharactersChanged, AOutlierPlayerState*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNodeCountChanged, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStatAllocatorExitPendingChanged, AOutlierPlayerState*);
DECLARE_MULTICAST_DELEGATE(FOnActivatedUpgradeNodesChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPendingPresetSelectionChanged, AOutlierPlayerState*);


/**
 * 
 */

UCLASS()
class OUTLIER_API AOutlierPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	void SetTemporaryPlayerId(const FGuid& NewPlayerId);

	UFUNCTION(BlueprintPure, Category = "Lobby")
	const FGuid& GetTemporaryPlayerId() const { return TemporaryPlayerId; }

	bool HasValidTemporaryPlayerId() const { return TemporaryPlayerId.IsValid(); }

	void SetCheckpointData(const FOutlierCheckpointData& NewData);
	const FOutlierCheckpointData& GetCheckpointData() const { return CheckpointData; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	AShooterCharacter* GetShooterCharacter() const;
	APartnerCharacter* GetPartnerCharacter() const;

	void SetShooterCharacter(AShooterCharacter* NewShooter);
	void SetPartnerCharacter(APartnerCharacter* NewPartner);

	void SetSuitDisabledByPartnerBoundary(bool bDisabled);
	bool IsSuitDisabledByPartnerBoundary() const { return bSuitDisabledByPartnerBoundary; }

	float GetPartnerDistance() const;

	UFUNCTION(BlueprintCallable, Category = "Pair")
	void SetPlayerRole(EOutlierPlayerRole NewRole);

	UFUNCTION(BlueprintPure, Category = "Pair")
	EOutlierPlayerRole GetPlayerRole() const { return PlayerRole; }
	bool IsShooterPlayer() const { return PlayerRole == EOutlierPlayerRole::Shooter; }
	bool IsPartnerPlayer() const { return PlayerRole == EOutlierPlayerRole::Partner; }

	FOnPlayerCharactersChanged OnPlayerCharactersChanged;

	UFUNCTION(BlueprintCallable, Category = "Pair")
	void SetPairId(int32 NewPairId);

	UFUNCTION(BlueprintPure, Category = "Pair")
	int32 GetPairId() const { return PairId; }

	UFUNCTION(BlueprintCallable, Category = "Node")
	bool AddNode(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Node")
	int32 GetNodeCount() const { return NodeCount; }

	UFUNCTION(BlueprintCallable, Category = "Node")
	bool ShareNode(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Node")
	bool ConsumeNode(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Stat Allocator")
	void SetStatAllocatorExitPending(bool bPending);

	UFUNCTION(BlueprintPure, Category = "Stat Allocator")
	bool IsStatAllocatorExitPending() const { return bStatAllocatorExitPending; }

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetPendingLobbyMatchId(int32 NewPendingLobbyMatchId);

	UFUNCTION(BlueprintPure, Category = "Lobby")
	int32 GetPendingLobbyMatchId() const { return PendingLobbyMatchId; }

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetPendingLobbyRole(EOutlierPlayerRole NewPendingLobbyRole);

	UFUNCTION(BlueprintPure, Category = "Lobby")
	EOutlierPlayerRole GetPendingLobbyRole() const { return PendingLobbyRole; }

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetPendingLobbySlotIndex(int32 NewPendingLobbySlotIndex);

	UFUNCTION(BlueprintPure, Category = "Lobby")
	int32 GetPendingLobbySlotIndex() const { return PendingLobbySlotIndex; }

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void ClearPendingLobbyState();

	UFUNCTION(BlueprintCallable, Category = "Upgrade")
	bool AddActivatedUpgradeNode(EOutlierUpgradeRole UpgradeRole, FName RowName);

	const TArray<FName>& GetActivatedUpgradeNodeIds(EOutlierUpgradeRole UpgradeRole) const;

	// 활성화된 업그레이드 노드 전부(Shooter/Partner 둘 다)를 비우고 NodeCount를 NewNodeCount로 덮어쓴다.
	// 프리셋 스테이지 확정 시 GameMode가 페어 양쪽 PlayerState에 호출한다.
	void FlushActivatedUpgradeNodes(int32 NewNodeCount);

	UFUNCTION(BlueprintCallable, Category = "Preset")
	void SetPendingPresetSelection(FName NewStageId);

	UFUNCTION(BlueprintPure, Category = "Preset")
	FName GetPendingPresetSelection() const { return PendingPresetSelection; }

protected:
	UPROPERTY(ReplicatedUsing = OnRep_PlayerRole)
	EOutlierPlayerRole PlayerRole = EOutlierPlayerRole::None;

	UPROPERTY(Replicated)
	int32 PairId = INDEX_NONE;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Node", meta = (ClampMin = "0"))
	int32 InitialNodeCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_NodeCount, VisibleInstanceOnly, BlueprintReadOnly, Category = "Node")
	int32 NodeCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_StatAllocatorExitPending, VisibleInstanceOnly, BlueprintReadOnly, Category = "Stat Allocator")
	bool bStatAllocatorExitPending = false;

	UPROPERTY(ReplicatedUsing = OnRep_PendingLobbyMatchId)
	int32 PendingLobbyMatchId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_PendingLobbyRole)
	EOutlierPlayerRole PendingLobbyRole = EOutlierPlayerRole::None;

	UPROPERTY(ReplicatedUsing = OnRep_PendingLobbySlotIndex)
	int32 PendingLobbySlotIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_CheckpointData)
	FOutlierCheckpointData CheckpointData;

	UPROPERTY(ReplicatedUsing = OnRep_ShooterCharacter)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_PartnerCharacter)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_SuitDisabledByPartnerBoundary)
	uint8 bSuitDisabledByPartnerBoundary : 1 = false;

	UPROPERTY(ReplicatedUsing = OnRep_ActivatedUpgradeNodes, VisibleInstanceOnly, BlueprintReadOnly, Category = "Upgrade")
	TArray<FName> ShooterActivatedUpgradeNodeIds;

	UPROPERTY(ReplicatedUsing = OnRep_ActivatedUpgradeNodes, VisibleInstanceOnly, BlueprintReadOnly, Category = "Upgrade")
	TArray<FName> PartnerActivatedUpgradeNodeIds;

	// 사망 후 프리셋 선택 대기 중인 값. 페어 양쪽에 리플리케이트돼 있어야 이후 "상대가 뭘 기다리는지" UI도 붙일 수 있다.
	UPROPERTY(ReplicatedUsing = OnRep_PendingPresetSelection)
	FName PendingPresetSelection = NAME_None;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Lobby")
	FGuid TemporaryPlayerId;

	UPROPERTY(ReplicatedUsing = OnRep_AcquiredSuit, VisibleInstanceOnly, BlueprintReadOnly, Category = "Suit")
	uint8 bHasAcquiredSuit : 1 = false;

	// 슈트 메시는 ASuitInteraction 이 들고 있는데 그 액터는 지급 직후 스스로 Destroy 한다.
	// 리로드로 Pawn 이 새로 스폰되면 AShooterCharacter::AppliedSuit*Mesh 도 함께 사라지므로,
	// "무엇을 입었는지"는 살아남는 PlayerState 가 기억해야 한다.
	// 적용은 서버에서만 하고, 클라는 캐릭터의 AppliedSuit*Mesh 복제로 반영되므로 복제하지 않는다.
	UPROPERTY()
	TObjectPtr<USkeletalMesh> SuitFirstPersonMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> SuitThirdPersonMesh;

	// 서버 전용 기록이다. 캡처(무기 획득)도 복원(리로드 후 재장착)도 서버에서만 일어나므로
	// 복제하지 않는다 (bHasAcquiredSuit 과 같은 이유). HUD가 읽어야 할 일이 생기면
	// 그때 COND_OwnerOnly 로 올린다.
	UPROPERTY()
	FOutlierLoadoutSnapshot LoadoutSnapshot;

protected:
	UFUNCTION()
	void OnRep_CheckpointData();

	UFUNCTION()
	void OnRep_ShooterCharacter();

	UFUNCTION()
	void OnRep_PartnerCharacter();

	UFUNCTION()
	void OnRep_SuitDisabledByPartnerBoundary();

	UFUNCTION()
	void OnRep_AcquiredSuit();

	void RefreshCharacterLinks();

	// 슈트 획득 상태를 페어 양쪽 캐릭터의 UI 로 투영한다.
	// 로컬 판정은 각 캐릭터가 하므로 여기서는 로컬 플레이어를 찾지 않는다.
	void RefreshPairSuitUI();

	UFUNCTION()
	void OnRep_PlayerRole();

	UFUNCTION()
	void OnRep_PendingLobbyMatchId();

	UFUNCTION()
	void OnRep_PendingLobbyRole();

	UFUNCTION()
	void OnRep_PendingLobbySlotIndex();

	UFUNCTION()
	void OnRep_NodeCount();

	UFUNCTION()
	void OnRep_StatAllocatorExitPending();

	UFUNCTION()
	void OnRep_ActivatedUpgradeNodes();

	UFUNCTION()
	void OnRep_PendingPresetSelection();

	UFUNCTION(Server, Reliable)
	void ServerSetStatAllocatorExitPending(bool bPending);

	void HandlePlayerRoleChanged();
	void HandlePendingLobbyStateChanged();
	void HandleStatAllocatorExitPendingChanged();
	void HandleActivatedUpgradeNodesChanged();
	void HandlePendingPresetSelectionChanged();
	void SetNodeCountInternal(int32 NewNodeCount);

public:
	FOnPlayerRoleChanged OnPlayerRoleChanged;
	FOnPendingLobbyStateChanged OnPendingLobbyStateChanged;
	FOnNodeCountChanged OnNodeCountChanged;
	FOnStatAllocatorExitPendingChanged OnStatAllocatorExitPendingChanged;
	FOnActivatedUpgradeNodesChanged OnActivatedUpgradeNodesChanged;

	void SetAcquiredSuit(bool Acquire);
	bool GetAcquiredSuit() const;

	// 페어 기준 슈트 획득 여부. 플래그는 Shooter PlayerState 에만 서므로,
	// Partner PlayerState 에서 물어도 짝의 Shooter 쪽을 찾아 돌려준다.
	// Partner 능력이 슈트에 의존하는데 Partner 는 자기 PS 만 들고 있어서 필요하다.
	bool IsPairSuitAcquired() const;

	void SetSuitMeshes(USkeletalMesh* FirstPersonMesh, USkeletalMesh* ThirdPersonMesh);
	USkeletalMesh* GetSuitFirstPersonMesh() const { return SuitFirstPersonMesh; }
	USkeletalMesh* GetSuitThirdPersonMesh() const { return SuitThirdPersonMesh; }

	void SetLoadoutSnapshot(const FOutlierLoadoutSnapshot& NewSnapshot);
	const FOutlierLoadoutSnapshot& GetLoadoutSnapshot() const { return LoadoutSnapshot; }
	// 재접속 시 새 PlayerState에 판 진행 데이터만 복원한다. 신원과 Pair 링크는 포함하지 않는다.
	void CopyReconnectGameplayStateFrom(const AOutlierPlayerState& Source);
	FOnPendingPresetSelectionChanged OnPendingPresetSelectionChanged;
};
