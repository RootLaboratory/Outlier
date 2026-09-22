// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/OutlierPostProcessVolume.h"

#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Curves/CurveFloat.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

void AOutlierPostProcessVolume::BeginPlay()
{
	Super::BeginPlay();

	InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType::Damaged);

	if (!HasStealthMeshMaterials())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s stealth mesh materials are not assigned."),
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

		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				if (ULocalPlayerPostProcessSubsystem* DoFSubsystem = LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
				{
					DoFSubsystem->SetDepthOfFieldVolume(this);
				}
			}
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
		&& !ScanFlagParameterName.IsNone();
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

void AOutlierPostProcessVolume::SetDamagedMaterialParameters(float InRatio)
{
}

void AOutlierPostProcessVolume::ResetPostProcessMaterialParameters()
{
	SetScanMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f);
	UpdateDamagedMaterialParameters(1.0f);
	ScanRangeRange = 0.0f;
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

	UKismetMaterialLibrary::SetVectorParameterValue(
		World,
		ScanParameterCollection,
		ScanLocationParameterName,
		FLinearColor(ScanLocation.X, ScanLocation.Y, ScanLocation.Z, 0.0f)
	);

	const float Progress = Range > 0.0f ? ScanRadius / Range : 0.0f;

	const float Flag = Range > 0.0f ? 1 : 0.0f;

	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanProgressParameterName,
		Progress
	);

	//기존 Range->
	UKismetMaterialLibrary::SetScalarParameterValue(
		World,
		ScanParameterCollection,
		ScanFlagParameterName,
		Flag
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

float AOutlierPostProcessVolume::EvaluateStealthFade(float InLinearFade) const
{
	const float LinearFade = FMath::Clamp(InLinearFade, 0.0f, 1.0f);

	// 양 끝은 커브를 태우지 않는다. 커브가 0/1 에서 어긋나 있어도 완전 은신 / 완전 복구는 보장돼야 한다.
	if (LinearFade <= 0.0f || LinearFade >= 1.0f || !StealthFadeCurve)
	{
		return LinearFade;
	}

	return FMath::Clamp(StealthFadeCurve->GetFloatValue(LinearFade), 0.0f, 1.0f);
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

void AOutlierPostProcessVolume::UpdateDamagedMaterialParameters(float InPlayerHPRatio, FVector4 Color) const
{
	const TObjectPtr<UMaterialInterface>* DamagedMaterial = PostProcessMaterials.Find(EOutlierPostProcessMaterialType::Damaged);
	UMaterialInstanceDynamic* DamagedMID = DamagedMaterial ? Cast<UMaterialInstanceDynamic>(DamagedMaterial->Get()) : nullptr;

	if (DamagedMID)
	{
		DamagedMID->SetScalarParameterValue(TEXT("HP_Portion"), FMath::Clamp(InPlayerHPRatio, 0.0f, 1.0f));
		DamagedMID->SetVectorParameterValue(TEXT("DamagedColor"), Color);

	}
}

void AOutlierPostProcessVolume::DisableAllBlendablesHard()
{
	for (FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
	{
		Blendable.Weight = 0.0f;
	}

	bScanPostProcessEnabled = false;
	bStealthPostProcessEnabled = false;
	bDamagedPostProcessEnabled = false;
}
