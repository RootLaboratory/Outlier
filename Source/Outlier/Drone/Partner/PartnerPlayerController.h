// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "PartnerPlayerController.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class AEnemyBase;
class AOutlierPlayerState;
class ULocalPlayerUISubSystem;

UENUM(BlueprintType)
enum class EPartnerPossessionState : uint8
{
	PartnerControlled,
	Transitioning,
	EnemyPossessed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnPartnerPossessionStateChanged,
	EPartnerPossessionState, PreviousState,
	EPartnerPossessionState, NewState,
	AActor*, ContextActor
);

/**
 * 
 */
UCLASS()
class OUTLIER_API APartnerPlayerController : public AFirstPersonPlayerController
{
	GENERATED_BODY()

protected:
	/** Pawn class used when respawning the player. */
	UPROPERTY(EditAnywhere, Category = "Partner|Respawn")
	TSubclassOf<APartnerCharacter> CharacterClass;

	/** Tag applied to the possessed player pawn. */
	UPROPERTY(EditAnywhere, Category = "Partner|Player")
	FName PartnerPawnTag = FName("Partner");

	/** Gameplay initialization */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;
	
	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;
	virtual void PawnPendingDestroy(APawn* InPawn) override;

	virtual void ReceivedPlayer() override;

	virtual void AcknowledgePossession(APawn* P) override;

	virtual void BindMainUI() override;

	virtual void BindPostProcessSubSystem() override;

	void RefreshShooterUIForRespawnFromPlayerState();
	void BindPlayerStateDelegates();
	void UnbindPlayerStateDelegates();
	void HandlePlayerCharactersChanged(AOutlierPlayerState* ChangedPlayerState);
	void BindShooterCharacterDelegatesFromPlayerState();
	void UnbindShooterCharacterDelegates();
	ULocalPlayerUISubSystem* GetLocalUISubsystem() const;

	void HandleShooterHealthChanged(float CurrentHealth, float MaxHealth);
	void HandleShooterShieldChanged(float CurrentShield, float MaxShield);
	void HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield);
	void HandleShooterConditionChanged(const FGameplayTag& ConditionTag);

	UPROPERTY()
	TObjectPtr<AShooterCharacter> BoundShooterCharacter;

	UPROPERTY()
	TObjectPtr<AOutlierPlayerState> BoundOutlierPlayerState;

	UPROPERTY()
	TWeakObjectPtr<APartnerCharacter> CachedPartnerCharacter;

	UPROPERTY(Transient)
	TWeakObjectPtr<AEnemyBase> PendingEnemyPossessionTarget; //Possess pending 동안 Controller 연출로 인해 Enemybase에 대한 처리를 Controller가 알아야 함. 

	UPROPERTY(Transient)
	TWeakObjectPtr<APartnerCharacter> PendingEnemyPossessionSource;

	UPROPERTY(Transient)
	TWeakObjectPtr<AEnemyBase> LocalPendingEnemyPossessionTarget;

	bool bHackTransitionInputBlocked = false;
	bool bHackTransitionCoveredNotified = false;

	void RestoreCachedPartnerCharacter();
	void RestoreCachedPartnerCharacterNextTick();
	void CommitPendingEnemyPossession(AEnemyBase* ExpectedTarget);
	void CancelPendingEnemyPossessionTransition();
	void CancelLocalEnemyPossessionTransition(AEnemyBase* ExpectedTarget);
	void BeginLocalEnemyPossessionReveal(APawn* InAcknowledgedPawn);
	void HandleHackPossessionTransitionFinished();
	void SetPartnerPossessionState(EPartnerPossessionState NewState, AActor* ContextActor);
	void SetHackTransitionInputBlocked(bool bBlocked);
	void HandlePartnerHackPossessionTransition();
	void BindPossessionTargetEndPlay(AEnemyBase* EnemyTarget);
	void UnbindPossessionTargetEndPlay(AEnemyBase* EnemyTarget);

	UFUNCTION()
	void HandlePossessionTargetEndPlay(
		AActor* EndedActor,
		EEndPlayReason::Type EndPlayReason);

	UFUNCTION(Client, Reliable)
	void ClientBeginEnemyPossessionTransition(AEnemyBase* EnemyTarget);

	UFUNCTION(Client, Reliable)
	void ClientCancelEnemyPossessionTransition(AEnemyBase* EnemyTarget);

	UFUNCTION(Server, Reliable)
	void ServerNotifyHackTransitionCovered(AEnemyBase* EnemyTarget);

	UFUNCTION(Server, Reliable)
	void ServerReleaseEnemyPossession();

public:
	APartnerPlayerController();

	UPROPERTY(BlueprintAssignable, Category = "Partner|EnemyPossession")
	FOnPartnerPossessionStateChanged OnPartnerPossessionStateChanged;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Partner|EnemyPossession")
	EPartnerPossessionState PartnerPossessionState = EPartnerPossessionState::PartnerControlled;

	void CachePartnerCharacterForEnemyPossession(APartnerCharacter* PartnerCharacter);
	bool BeginEnemyPossessionTransition(AEnemyBase* EnemyTarget, APartnerCharacter* PartnerCharacter);

	UFUNCTION(BlueprintPure, Category = "Partner|EnemyPossession")
	EPartnerPossessionState GetPartnerPossessionState() const { return PartnerPossessionState; }

	UFUNCTION(BlueprintCallable, Category = "Partner|EnemyPossession")
	void NotifyHackTransitionCovered();

	UFUNCTION(BlueprintCallable, Category = "Partner|EnemyPossession")
	void ReleaseEnemyPossession();

	/** 로그아웃 정리용 — 캐시된 원래 Partner 캐릭터를 꺼내면서 캐시를 비운다. 빙의를 복원하지는 않는다. */
	APartnerCharacter* ExtractCachedPartnerCharacterForLogout();
};
