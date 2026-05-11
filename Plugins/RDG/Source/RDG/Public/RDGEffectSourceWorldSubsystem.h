// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RDGEffectSourceWorldSubsystem.generated.h"

/**
 * 
 */
class UHeatHazeSourceComponent;
struct FHeatHazeSourceData;

UCLASS()
class RDG_API URDGEffectSourceWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
public:
	void RegisterSource(UHeatHazeSourceComponent* Source);
	void UnregisterSource(UHeatHazeSourceComponent* Source);

	void GatherHeatHazeSources(TArray<FHeatHazeSourceData>& OutSources) const;
private:

	void CompactHeatSources();

	UPROPERTY()
	TArray<TWeakObjectPtr<UHeatHazeSourceComponent>> HeatSources;
};
