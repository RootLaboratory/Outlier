// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "OutlierCheckpoint.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class AController;
class AOutlierCheckpoint;
class AOutlierPlayerState;
class UPrimitiveComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnOutlierCheckpointCommitted, AOutlierCheckpoint*);

UCLASS()
class OUTLIER_API AOutlierCheckpoint : public AActor
{
	GENERATED_BODY()
	
public:	
	AOutlierCheckpoint();

	FName GetCheckpointId() const { return CheckpointId; }
	FTransform GetSpawnTransform() const;
	FTransform GetPartnerSpawnTransform() const;
	FGameplayTag GetCombatRoomTag() const { return CombatRoomTag; }
	FOnOutlierCheckpointCommitted OnCheckpointCommitted;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Checkpoint")
	bool SetActivationConditionSatisfied(AController* ActivatingController, bool bSatisfied = true);
	void SetCombatRoomTag(FGameplayTag InRoomTag);
	bool RetryCommit();

	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool IsCheckpointCommitted() const { return bCheckpointCommitted; }

protected:
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Checkpoint")
	bool bInitiallyActive = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Checkpoint")
	FName CheckpointId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	FVector PartnerSpawnOffset = FVector(0.0f, 150.0f, 0.0f);

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Checkpoint")
	bool bActivationConditionSatisfied = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Checkpoint")
	bool bCheckpointCommitted = false;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	TObjectPtr<UStaticMeshComponent> CheckpointMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	TObjectPtr<USceneComponent> SpawnPoint;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	friend class FOutlierCheckpointActivationOverlapTest;

	UFUNCTION()
	void HandleTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);
	void ProcessTriggerOverlap(AActor* OtherActor, UPrimitiveComponent* OtherComp);
	AOutlierPlayerState* ResolvePairPlayerState(AActor* Actor) const;
	bool TryCommit();

	bool bCheckpointIdRegistered = false;
	bool bActivationExplicitlySet = false;
	int32 OverlappingPairId = INDEX_NONE;
	bool bShooterPassed = false;
	bool bPartnerPassed = false;
	TWeakObjectPtr<AController> CommitController;
	FGameplayTag CombatRoomTag;
};
