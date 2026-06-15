// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/OutlierPostProcessVolume.h"

#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

void AOutlierPostProcessVolume::BeginPlay()
{
	Super::BeginPlay();

	InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType::Damaged);

	if (!HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Stealth))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s stealth post-process material is invalid."),
			*GetName()
		);
	}

	if (!HasValidScanPostProcessBindings())
	{
		const TObjectPtr<UMaterialInterface>* ScanPostProcessMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Scan);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s scan post-process bindings are invalid. Material=%s MPC=%s RadiusParam=%s LocationParam=%s"),
			*GetName(),
			*GetNameSafe(ScanPostProcessMaterial ? ScanPostProcessMaterial->Get() : nullptr),
			*GetNameSafe(ScanParameterCollection),
			*ScanRadiusParameterName.ToString(),
			*ScanLocationParameterName.ToString()
		);
	}

	if (UWorld* World = GetWorld())
	{
		if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PostProcessSubsystem->RegisterPostProcessVolume(this);
		}
	}
}

void AOutlierPostProcessVolume::InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType MaterialType)
{
	TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	if (!PostProcessMaterial || !PostProcessMaterial->Get() || Cast<UMaterialInstanceDynamic>(PostProcessMaterial->Get()))
	{
		return;
	}

	UMaterialInterface* SourceMaterial = PostProcessMaterial->Get();
	UMaterialInstanceDynamic* RuntimeMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
	if (!RuntimeMaterial)
	{
		return;
	}

	for (FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
	{
		if (Blendable.Object.Get() == SourceMaterial)
		{
			Blendable.Object = RuntimeMaterial;
		}
	}

	*PostProcessMaterial = RuntimeMaterial;
}

bool AOutlierPostProcessVolume::HasValidScanPostProcessBindings() const
{
	return HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Scan)
		&& ScanParameterCollection
		&& !ScanRadiusParameterName.IsNone()
		&& !ScanProgressParameterName.IsNone()
		&& !ScanLocationParameterName.IsNone()
		&& !ScanRangeParameterName.IsNone();
}

bool AOutlierPostProcessVolume::HasValidPostProcessMaterial(EOutlierPostProcessMaterialType MaterialType) const
{
	const TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	return PostProcessMaterial && PostProcessMaterial->Get();
}

void AOutlierPostProcessVolume::SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bInEnabled)
{
	const float TargetWeight = bInEnabled ? 1.0f : 0.0f;
	if (!SetBlendableWeight(MaterialType, TargetWeight))
	{
		return;
	}

	switch (MaterialType)
	{
	case EOutlierPostProcessMaterialType::Scan:
		bScanPostProcessEnabled = bInEnabled;
		//UE_LOG(LogTemp, Error, TEXT("[ScanPPDebug] SetEnabled=%d Weight=%.2f"), bScanPostProcessEnabled ? 1 : 0, TargetWeight);
		break;
	case EOutlierPostProcessMaterialType::Stealth:
		bStealthPostProcessEnabled = bInEnabled;
		//UE_LOG(LogTemp, Error, TEXT("[StealthPPDebug] SetEnabled=%d Weight=%.2f"), bStealthPostProcessEnabled ? 1 : 0, TargetWeight);
		break;
	case EOutlierPostProcessMaterialType::Damaged:
		bDamagedPostProcessEnabled = bInEnabled;
		//UE_LOG(LogTemp, Error, TEXT("[DamagedPPDebug] SetEnabled=%d Weight=%.2f"), bDamagedPostProcessEnabled ? 1 : 0, TargetWeight);
		break;
	default:
		break;
	}
}

void AOutlierPostProcessVolume::SetScanPostProcessEnabled(bool bEnableScanPostProcess)
{
	if (!HasValidScanPostProcessBindings())
	{
		return;
	}

	SetPostProcessEnabled(EOutlierPostProcessMaterialType::Scan, bEnableScanPostProcess);
}

void AOutlierPostProcessVolume::SetStealthPostProcessEnabled(bool bEnabledStealthPostProcess)
{
	SetPostProcessEnabled(EOutlierPostProcessMaterialType::Stealth, bEnabledStealthPostProcess);
}


bool AOutlierPostProcessVolume::SetBlendableWeight(EOutlierPostProcessMaterialType MaterialType, float Weight)
{
	const TObjectPtr<UMaterialInterface>* PostProcessMaterial = PostProcessMaterials.Find(MaterialType);
	if (!PostProcessMaterial || !PostProcessMaterial->Get())
	{
		return false;
	}

	const float ClampedWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
	TArray<FWeightedBlendable>& Blendables = Settings.WeightedBlendables.Array;
	for (FWeightedBlendable& Blendable : Blendables)
	{
		if (Blendable.Object.Get() == PostProcessMaterial->Get())
		{
			Blendable.Weight = ClampedWeight;
			return true;
		}
	}

	Blendables.Add(FWeightedBlendable(ClampedWeight, PostProcessMaterial->Get()));
	return true;
}

void AOutlierPostProcessVolume::SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range) 
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

	const float Progress = Range > 0.0f ? ScanRadius / Range : 0.0f;

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanProgressParameterName,
		Progress
	);

	ScanRangeRange = Range;

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

	const float Progress = ScanRangeRange > 0.0f ? ScanRadius / ScanRangeRange : 0.0f;

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanProgressParameterName,
		Progress
	);
}

void AOutlierPostProcessVolume::UpdateDamagedMaterialParameters(float InPlayerHPRatio)  const
{
	const TObjectPtr<UMaterialInterface>* DamagedMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Damaged);
	UMaterialInstanceDynamic* DamagedMID = DamagedMaterial ? Cast<UMaterialInstanceDynamic>(DamagedMaterial->Get()) : nullptr;

	if (DamagedMID)
	{
		DamagedMID->SetScalarParameterValue(TEXT("HP_Portion"), FMath::Clamp(InPlayerHPRatio, 0.0f, 1.0f));
	}
}
