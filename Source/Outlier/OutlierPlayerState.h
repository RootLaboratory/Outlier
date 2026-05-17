// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Save/OutlierCheckpointData.h"
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


/**
 * 
 */

UCLASS()
class OUTLIER_API AOutlierPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
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

	UFUNCTION(BlueprintCallable, Category = "Pair")
	void SetPairId(int32 NewPairId);

	UFUNCTION(BlueprintPure, Category = "Pair")
	int32 GetPairId() const { return PairId; }

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
	void ClearPendingLobbyState();

	UPROPERTY(Replicated)
	int32 ArenaId = INDEX_NONE;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_PlayerRole)
	EOutlierPlayerRole PlayerRole = EOutlierPlayerRole::None;

	UPROPERTY(Replicated)
	int32 PairId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_PendingLobbyMatchId)
	int32 PendingLobbyMatchId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_PendingLobbyRole)
	EOutlierPlayerRole PendingLobbyRole = EOutlierPlayerRole::None;

	UPROPERTY(ReplicatedUsing = OnRep_CheckpointData)
	FOutlierCheckpointData CheckpointData;

	UPROPERTY(ReplicatedUsing = OnRep_ShooterCharacter)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_PartnerCharacter)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_SuitDisabledByPartnerBoundary)
	uint8 bSuitDisabledByPartnerBoundary : 1 = false;


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

	void HandlePlayerRoleChanged();
	void HandlePendingLobbyStateChanged();

public:

	FOnPlayerRoleChanged OnPlayerRoleChanged;
	FOnPendingLobbyStateChanged OnPendingLobbyStateChanged;
};
