// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OutlierCheckpoint.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class AController;

UCLASS()
class OUTLIER_API AOutlierCheckpoint : public AActor
{
	GENERATED_BODY()
	
public:	
	AOutlierCheckpoint();

	FName GetCheckpointId() const { return CheckpointId; }
	FTransform GetSpawnTransform() const;
	FTransform GetPartnerSpawnTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Checkpoint")
	bool SetActivationConditionSatisfied(AController* ActivatingController, bool bSatisfied = true);

	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool IsCheckpointCommitted() const { return bCheckpointCommitted; }

protected:
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
	bool bCheckpointIdRegistered = false;
};
