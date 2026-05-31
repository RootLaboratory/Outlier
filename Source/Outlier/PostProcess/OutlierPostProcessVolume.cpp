// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/OutlierPostProcessVolume.h"

#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInterface.h"

void AOutlierPostProcessVolume::BeginPlay()
{
	Super::BeginPlay();

	if (!HasValidScanPostProcessBindings())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s scan post-process bindings are invalid. Material=%s MPC=%s RadiusParam=%s LocationParam=%s"),
			*GetName(),
			*GetNameSafe(ScanPostProcessMaterial),
			*GetNameSafe(ScanParameterCollection),
			*ScanRadiusParameterName.ToString(),
			*ScanLocationParameterName.ToString()
		);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PostProcessSubsystem->RegisterScanPostProcessVolume(this);
		}
	}
}

bool AOutlierPostProcessVolume::HasValidScanPostProcessBindings() const
{
	return ScanPostProcessMaterial
		&& ScanParameterCollection
		&& !ScanRadiusParameterName.IsNone()
		&& !ScanLocationParameterName.IsNone();
}

void AOutlierPostProcessVolume::SetScanPostProcessEnabled(bool bEnableScanPostProcess)
{
	if (!HasValidScanPostProcessBindings())
	{
		return;
	}

	const float TargetWeight = bEnableScanPostProcess ? 1.0f : 0.0f;
	if (SetScanBlendableWeight(TargetWeight))
	{
		bScanPostProcessEnabled = bEnableScanPostProcess;
		UE_LOG(LogTemp, Error, TEXT("[ScanPPDebug] SetEnabled=%d Weight=%.2f"), bScanPostProcessEnabled ? 1 : 0, TargetWeight);
	}
}

bool AOutlierPostProcessVolume::SetScanBlendableWeight(float Weight)
{
	if (!ScanPostProcessMaterial)
	{
		return false;
	}

	TArray<FWeightedBlendable>& Blendables = Settings.WeightedBlendables.Array;
	for (FWeightedBlendable& Blendable : Blendables)
	{
		if (Blendable.Object.Get() == ScanPostProcessMaterial.Get())
		{
			Blendable.Weight = Weight;
			return true;
		}
	}

	Blendables.Add(FWeightedBlendable(Weight, ScanPostProcessMaterial.Get()));
	return true;
}

void AOutlierPostProcessVolume::SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range) const
{
	UWorld* World = GetWorld();
	if (!World || !HasValidScanPostProcessBindings())
	{
		return;
	}

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanRadiusParameterName,
		ScanRadius
	);

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanRangeParameterName,
		Range
	);

	UKismetMaterialLibrary::SetVectorParameterValue(
		World,
		ScanParameterCollection,
		ScanLocationParameterName,
		FLinearColor(ScanLocation.X, ScanLocation.Y, ScanLocation.Z, 0.0f)
	);

	//UE_LOG(LogTemp, Error, TEXT("[ScanPPDebug] Set Params Radius=%.2f Range=%.2f Location=%s"), ScanRadius, Range, *ScanLocation.ToString());

}

void AOutlierPostProcessVolume::UpdateScanMaterialParameters(FVector ScanLocation, float ScanRadius) const
{
	UWorld* World = GetWorld();
	if (!World || !HasValidScanPostProcessBindings())
	{
		return;
	}

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanRadiusParameterName,
		ScanRadius
	);

	UKismetMaterialLibrary::SetVectorParameterValue(
		World,
		ScanParameterCollection,
		ScanLocationParameterName,
		FLinearColor(ScanLocation.X, ScanLocation.Y, ScanLocation.Z, 0.0f)
	);

	//UE_LOG(LogTemp, Error, TEXT("[ScanPPDebug] Update Params Radius=%.2f Location=%s"), ScanRadius, *ScanLocation.ToString());
}
