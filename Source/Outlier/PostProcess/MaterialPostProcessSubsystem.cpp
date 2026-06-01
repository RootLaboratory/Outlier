// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/MaterialPostProcessSubsystem.h"

#include "PostProcess/OutlierPostProcessVolume.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void UMaterialPostProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMaterialPostProcessSubsystem::Deinitialize()
{
	EndScanPostProcess();

	Super::Deinitialize();
}

void UMaterialPostProcessSubsystem::RegisterScanPostProcessVolume(AOutlierPostProcessVolume* InPostProcessVolume)
{
	if (!InPostProcessVolume || !InPostProcessVolume->HasValidScanPostProcessBindings())
	{
		return;
	}

	BoundScanPostProcessVolume = InPostProcessVolume;
}

void UMaterialPostProcessSubsystem::StartScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius,
	float Range)
{
	if (ShouldSkipRenderingWork() || !BoundScanPostProcessVolume)
	{
		return;
	}

	BoundScanPostProcessVolume->SetScanMaterialParameters(ScanOrigin, CurrentScanRadius, Range);
	BoundScanPostProcessVolume->SetScanPostProcessEnabled(true);
}

void UMaterialPostProcessSubsystem::UpdateScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius)
{

	//DrawDebugSphere(
	//	GetWorld(),
	//	ScanOrigin,
	//	CurrentScanRadius,
	//	32,
	//	FColor::Cyan,
	//	false,
	//	0.25f,
	//	0,
	//	2.0f
	//);

	if (ShouldSkipRenderingWork() || !BoundScanPostProcessVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("BoundScanPostProcessVolume inValid"));
		return;
	}

	BoundScanPostProcessVolume->UpdateScanMaterialParameters(ScanOrigin, CurrentScanRadius);
}

void UMaterialPostProcessSubsystem::EndScanPostProcess()
{
	if (ShouldSkipRenderingWork())
	{
		return;
	}

	if (BoundScanPostProcessVolume)
	{
		BoundScanPostProcessVolume->SetScanPostProcessEnabled(false);
		BoundScanPostProcessVolume->SetScanMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f);
	}
	ClearAllScanStencils();
}

void UMaterialPostProcessSubsystem::ApplyScanStencil(AActor* Actor, int32 StencilValue)
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
