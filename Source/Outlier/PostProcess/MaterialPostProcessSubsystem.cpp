// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/MaterialPostProcessSubsystem.h"

#include "PostProcess/OutlierPostProcessVolume.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void UMaterialPostProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Refresh();
}

void UMaterialPostProcessSubsystem::Deinitialize()
{
	Super::Deinitialize();
	Refresh();
}

void UMaterialPostProcessSubsystem::RegisterPostProcessVolume(AOutlierPostProcessVolume* InPostProcessVolume)
{
	if (!InPostProcessVolume)
	{
		return;
	}

	if (!InPostProcessVolume->HasValidScanPostProcessBindings()
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Stealth)
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Damaged))
	{
		return;
	}

	BoundPostProcessVolume = InPostProcessVolume;
}

void UMaterialPostProcessSubsystem::SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bEnabled)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->SetPostProcessEnabled(MaterialType, bEnabled);
}

void UMaterialPostProcessSubsystem::Refresh()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->DisableAllBlendablesHard();
	FlushPostProcessMaterialParameters();
	FlushScanStencilRestoreStates();
}

void UMaterialPostProcessSubsystem::StartScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius,
	float Range)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->SetScanMaterialParameters(ScanOrigin, CurrentScanRadius, Range);
	BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Scan ,true);
}

void UMaterialPostProcessSubsystem::UpdateScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius)
{

	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("BoundPostProcessVolume inValid"));
		return;
	}

	BoundPostProcessVolume->UpdateScanMaterialParameters(ScanOrigin, CurrentScanRadius);
}

void UMaterialPostProcessSubsystem::UpdateDamagedPostProcess(float InHPRatio)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->UpdateDamagedMaterialParameters(InHPRatio);
}

void UMaterialPostProcessSubsystem::UpdateDamagedPostProcess(float InHPRatio, FVector4 Color)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->UpdateDamagedMaterialParameters(InHPRatio, Color);
}

void UMaterialPostProcessSubsystem::EndScanPostProcess()
{
	if (ShouldSkipRenderingWork())
	{
		return;
	}

	if (BoundPostProcessVolume)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Scan, false);
		BoundPostProcessVolume->SetScanMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f);
	}
	ClearAllScanStencils();
}

void UMaterialPostProcessSubsystem::EndDamagedPostProcess()
{
	if (ShouldSkipRenderingWork())
	{
		return;
	}

	if (BoundPostProcessVolume)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, false);
		BoundPostProcessVolume->UpdateDamagedMaterialParameters(1.0f);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BoundPostProcessVolume"))

	}
}

void UMaterialPostProcessSubsystem::ApplyScanStencil(AActor* Actor, int32 StencilValue)
{
	if (ShouldSkipRenderingWork() || !Actor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	//BP에서 설정하면 상관없긴 하다만.

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		TWeakObjectPtr<UPrimitiveComponent> ComponentKey(PrimitiveComponent);
		if (!ScanStencilRestoreStates.Contains(ComponentKey))
		{
			FScanStencilRestoreState RestoreState;
			RestoreState.bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
			RestoreState.CustomDepthStencilValue = PrimitiveComponent->CustomDepthStencilValue;
			ScanStencilRestoreStates.Add(ComponentKey, RestoreState);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
	}
}

void UMaterialPostProcessSubsystem::ClearScanStencil(AActor* Actor)
{
	if (ShouldSkipRenderingWork() || !Actor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		TWeakObjectPtr<UPrimitiveComponent> ComponentKey(PrimitiveComponent);
		if (const FScanStencilRestoreState* RestoreState = ScanStencilRestoreStates.Find(ComponentKey))
		{
			PrimitiveComponent->SetRenderCustomDepth(RestoreState->bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(RestoreState->CustomDepthStencilValue);
			ScanStencilRestoreStates.Remove(ComponentKey);
		}
	}
}

bool UMaterialPostProcessSubsystem::ShouldSkipRenderingWork() const
{
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() == NM_DedicatedServer;
}

void UMaterialPostProcessSubsystem::ClearAllScanStencils()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState>& Pair : ScanStencilRestoreStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get())
		{
			PrimitiveComponent->SetRenderCustomDepth(Pair.Value.bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(Pair.Value.CustomDepthStencilValue);
		}
	}

	ScanStencilRestoreStates.Reset();
}

void UMaterialPostProcessSubsystem::DisableAllBoundPostProcessMaterials()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	for (const TPair<EOutlierPostProcessMaterialType, TObjectPtr<UMaterialInterface>>& Pair
		: BoundPostProcessVolume->PostProcessMaterials)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(Pair.Key, false);
	}

}

void UMaterialPostProcessSubsystem::FlushScanStencilRestoreStates()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState>& Pair
		: ScanStencilRestoreStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get())
		{
			PrimitiveComponent->SetRenderCustomDepth(Pair.Value.bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(Pair.Value.CustomDepthStencilValue);
		}
	}

	ScanStencilRestoreStates.Reset();
}

void UMaterialPostProcessSubsystem::FlushPostProcessMaterialParameters()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->ResetPostProcessMaterialParameters();
}
