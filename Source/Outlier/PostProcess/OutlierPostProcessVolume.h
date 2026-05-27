// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "OutlierPostProcessVolume.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;

UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API AOutlierPostProcessVolume : public APostProcessVolume
{
	GENERATED_BODY()

public:
	bool HasValidScanPostProcessBindings() const;
	void SetScanPostProcessEnabled(bool bEnableScanPostProcess);
	void SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range) const;
	void UpdateScanMaterialParameters(FVector ScanLocation, float ScanRadius) const;

protected:
	virtual void BeginPlay() override;

private:
	bool SetScanBlendableWeight(float Weight);


	//Scan
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> ScanPostProcessMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialParameterCollection> ScanParameterCollection;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanRadiusParameterName = TEXT("Radius");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanLocationParameterName = TEXT("Location");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanRangeParameterName = TEXT("Range");


	bool bScanPostProcessEnabled = false;
};
