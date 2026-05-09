// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "OutlierCheckpointData.h"
#include "OutlierPlayerState.generated.h"

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

protected:
	UPROPERTY(ReplicatedUsing = OnRep_CheckpointData)
	FOutlierCheckpointData CheckpointData;

	UFUNCTION()
	void OnRep_CheckpointData();

};
