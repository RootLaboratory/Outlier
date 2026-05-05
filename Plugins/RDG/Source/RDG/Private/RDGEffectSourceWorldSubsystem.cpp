// Fill out your copyright notice in the Description page of Project Settings.


#include "RDGEffectSourceWorldSubsystem.h"
#include "HeatHazeSourceComponent.h"

void URDGEffectSourceWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	HeatSources.Reset();
}

void URDGEffectSourceWorldSubsystem::Deinitialize()
{
	HeatSources.Reset();

	Super::Deinitialize();
}

void URDGEffectSourceWorldSubsystem::RegisterSource(UHeatHazeSourceComponent* Source)
{
	if (!Source)
	{
		return;
	}

	CompactHeatSources();

	if (!HeatSources.Contains(Source))
	{
		HeatSources.Add(Source);
	}
}

void URDGEffectSourceWorldSubsystem::UnregisterSource(UHeatHazeSourceComponent* Source)
{
	HeatSources.RemoveAllSwap(
		[Source](const TWeakObjectPtr<UHeatHazeSourceComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Source;
		});
}

void URDGEffectSourceWorldSubsystem::GatherHeatHazeSources(TArray<FHeatHazeSourceData>& OutSources) const
{
	OutSources.Reset();

	for (const TWeakObjectPtr<UHeatHazeSourceComponent>& SourcePtr : HeatSources)
	{
		const UHeatHazeSourceComponent* Source = SourcePtr.Get();
		if (!Source)
		{
			continue;
		}

		FHeatHazeSourceData SourceData;
		if (Source->BuildSourceData(SourceData))
		{
			OutSources.Add(SourceData);
		}
	}
}

void URDGEffectSourceWorldSubsystem::CompactHeatSources()
{
	HeatSources.RemoveAllSwap(
		[](const TWeakObjectPtr<UHeatHazeSourceComponent>& Entry)
		{
			return !Entry.IsValid();
		});
	//RemoveAllSwap 순서 보장을 할 필요가 없기에 사용.
}
