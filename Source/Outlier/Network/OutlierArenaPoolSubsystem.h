// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierArenaInstance.h"
#include "OutlierArenaPoolSubsystem.generated.h"

class ULevelStreamingDynamic;
class ULevel;

/**
 * 
 */
UCLASS()
class OUTLIER_API UOutlierArenaPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FOutlierArenaInstance* AcquireArena();
	void ReleaseArena(int32 ArenaId);
	ULevel* GetArenaLoadedLevel(int32 ArenaId) const;

private:
	void PreloadArenas();
	ULevelStreamingDynamic* LoadArenaLevelInstance(int32, const FTransform& InstanceTransform);

	UPROPERTY()
	TArray<FOutlierArenaInstance> Arenas;

	UPROPERTY(EditDefaultsOnly, Category = "Arena")
	TSoftObjectPtr<UWorld> ArenaLevel;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	int32 MaxArenaCount = 5;
};
