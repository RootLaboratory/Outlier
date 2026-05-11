// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OutlierCheckpointData.h"
#include "OutlierSaveSubSystem.generated.h"

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierSaveSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	bool SavePlayerCheckpoint(const FString& PlayerId, const FOutlierCheckpointData& Data);
	bool LoadPlayerCheckpoint(const FString& PlayerId, FOutlierCheckpointData& OutData) const;

private:
	TMap<FString, FOutlierCheckpointData> RuntimeCheckpointData;
};
