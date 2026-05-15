// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OutlierCheckpoint.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API AOutlierCheckpoint : public AActor
{
	GENERATED_BODY()
	
public:	
	AOutlierCheckpoint();

	FName GetCheckpointId() const { return CheckpointId; }
	FTransform GetSpawnTransform() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	FName CheckpointId = NAME_None;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	TObjectPtr<UStaticMeshComponent> CheckpointMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	TObjectPtr<USceneComponent> SpawnPoint;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
};
