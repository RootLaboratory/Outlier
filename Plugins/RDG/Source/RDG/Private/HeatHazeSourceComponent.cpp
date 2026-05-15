// Fill out your copyright notice in the Description page of Project Settings.


#include "HeatHazeSourceComponent.h"
#include "RDGEffectSourceWorldSubsystem.h"

// Sets default values for this component's properties
UHeatHazeSourceComponent::UHeatHazeSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeatHazeSourceComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld())
	{
		URDGEffectSourceWorldSubsystem* Subsystem = World->GetSubsystem< URDGEffectSourceWorldSubsystem>();
		if (Subsystem)
		{
			Subsystem->RegisterSource(this);
			//UE_LOG(LogTemp, Error, TEXT("Registered"));
		}
	}
}

void UHeatHazeSourceComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (URDGEffectSourceWorldSubsystem* Subsystem = World->GetSubsystem<URDGEffectSourceWorldSubsystem>())
		{
			Subsystem->UnregisterSource(this);
		}
	}

	Super::OnUnregister();
}

bool UHeatHazeSourceComponent::BuildSourceData(FHeatHazeSourceData& OutData) const
{
	if (!bEnabled || Strength <= 0.0f)
	{
		return false;
	}

	OutData.Shape = Shape;
	OutData.Transform = FTransform3f(GetComponentTransform());
	OutData.Size = FVector2f(BoxExtent.X, BoxExtent.Y);
	OutData.Strength = Strength;
	OutData.MaxPixelOffset = MaxPixelOffset;
	OutData.EdgeSoftness = EdgeSoftness;
	OutData.FalloffExponent = FalloffExponent;
	OutData.NoiseScale = NoiseScale;
	OutData.NoiseSpeed = NoiseSpeed;
	OutData.NoiseDirection = FVector2f(NoiseDirection);
	OutData.DepthFadeDistance = DepthFadeDistance;
	OutData.bUseDepthOcclusion = bUseDepthOcclusion ? 1u : 0u;

	return true;
}
