// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Save/OutlierCheckpointData.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "OutlierPlayerState.generated.h"

class AShooterCharacter;
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

	UFUNCTION(BlueprintCallable, Category = "Pair")
	void SetArenaId(int32 NewArenaId);

	UFUNCTION(BlueprintPure, Category = "Pair")
	int32 GetArenaId() const { return ArenaId; }

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

	UPROPERTY(Replicated)
	int32 ArenaId = INDEX_NONE;

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

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Lobby")
	FGuid TemporaryPlayerId;

protected:
	UFUNCTION()
	void OnRep_CheckpointData();

	UFUNCTION()
	void OnRep_ShooterCharacter();

	UFUNCTION()
	void OnRep_PartnerCharacter();

	UFUNCTION()
	void OnRep_SuitDisabledByPartnerBoundary();

	void RefreshCharacterLinks();

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

	UFUNCTION(Server, Reliable)
	void ServerSetStatAllocatorExitPending(bool bPending);

	void HandlePlayerRoleChanged();
	void HandlePendingLobbyStateChanged();
	void HandleStatAllocatorExitPendingChanged();
	void HandleActivatedUpgradeNodesChanged();
	void SetNodeCountInternal(int32 NewNodeCount);

public:

	FOnPlayerRoleChanged OnPlayerRoleChanged;
	FOnPendingLobbyStateChanged OnPendingLobbyStateChanged;
	FOnNodeCountChanged OnNodeCountChanged;
	FOnStatAllocatorExitPendingChanged OnStatAllocatorExitPendingChanged;
	FOnActivatedUpgradeNodesChanged OnActivatedUpgradeNodesChanged;
};
