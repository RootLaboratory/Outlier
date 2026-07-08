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
struct FGameplayTag;
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
	
	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

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

	void RestoreCachedPartnerCharacter();
	void RestoreCachedPartnerCharacterNextTick();

	UFUNCTION(Server, Reliable)
	void ServerReleaseEnemyPossession();

public:
	APartnerPlayerController();

	void CachePartnerCharacterForEnemyPossession(APartnerCharacter* PartnerCharacter);

	UFUNCTION(BlueprintCallable, Category = "Partner|EnemyPossession")
	void ReleaseEnemyPossession();
	
	
};
