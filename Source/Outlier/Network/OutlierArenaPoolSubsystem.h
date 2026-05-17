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
	void EnsureArenaLoaded(int32 ArenaId); // ps id replcated되고, 해당 id로 arena 이동; Client는 id 에 해당하는 arena만 load 

private:
	void PreloadArenas();
	ULevelStreamingDynamic* LoadArenaLevelInstance(int32, const FTransform& InstanceTransform);
	void RefreshArenaReadyStates(const TCHAR* Reason);

	UFUNCTION()
	void HandleArenaLevelLoaded();

	UFUNCTION()
	void HandleArenaLevelShown();


	UPROPERTY()
	TArray<FOutlierArenaInstance> Arenas;

	UPROPERTY(EditDefaultsOnly, Category = "Arena")
	TSoftObjectPtr<UWorld> ArenaLevel;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	int32 MaxArenaCount = 1;
};
