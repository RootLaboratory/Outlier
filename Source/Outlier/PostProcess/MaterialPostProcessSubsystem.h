// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MaterialPostProcessSubsystem.generated.h"

class AOutlierPostProcessVolume;
class UPrimitiveComponent;

struct FScanStencilRestoreState
{
	bool bRenderCustomDepth = false;
	int32 CustomDepthStencilValue = 0;
};

UCLASS()
class OUTLIER_API UMaterialPostProcessSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterScanPostProcessVolume(AOutlierPostProcessVolume* InPostProcessVolume);

	void StartScanPostProcess(FVector ScanOrigin, float CurrentScanRadius, float Range);

	void UpdateScanPostProcess(FVector ScanOrigin, float CurrentScanRadius);

	void EndScanPostProcess();

	void ApplyScanStencil(AActor* Actor, int32 StencilValue);

	void ClearScanStencil(AActor* Actor);

private:
	UPROPERTY()
	TObjectPtr<AOutlierPostProcessVolume> BoundScanPostProcessVolume;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState> ScanStencilRestoreStates;

	bool ShouldSkipRenderingWork() const;
	void ClearAllScanStencils();
};
