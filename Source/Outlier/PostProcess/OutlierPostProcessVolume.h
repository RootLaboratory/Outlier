// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "OutlierPostProcessVolume.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class EOutlierPostProcessMaterialType : uint8
{
	Scan UMETA(DisplayName = "Scan"),
	Stealth UMETA(DisplayName = "Stealth"),
	Damaged UMETA(DisplayName = "Damaged")

};

UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API AOutlierPostProcessVolume : public APostProcessVolume
{
	GENERATED_BODY()

public:
	bool HasValidScanPostProcessBindings() const;
	bool HasValidPostProcessMaterial(EOutlierPostProcessMaterialType MaterialType) const;
	void SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bInEnabled);
	
	void SetDamagedMaterialParameters(float InRatio);
	void SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range);
	void ResetPostProcessMaterialParameters();

	void UpdateScanMaterialParameters(FVector ScanLocation, float ScanRadius) const;
	void UpdateDamagedMaterialParameters(float InPlayerHPRatio) const;
	void UpdateDamagedMaterialParameters(float InPlayerHPRatio, FVector4 Color) const;
	void DisableAllBlendablesHard();

protected:
	virtual void BeginPlay() override;

private:
	bool SetBlendableWeight(EOutlierPostProcessMaterialType MaterialType, float Weight);
	void InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType MaterialType);

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (AllowPrivateAccess = "true"))
	TMap<EOutlierPostProcessMaterialType, TObjectPtr<UMaterialInterface>> PostProcessMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialParameterCollection> ScanParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth")
	uint8 StealthStencilNumber = 5;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanRadiusParameterName = TEXT("Radius");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanProgressParameterName = TEXT("Progress");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanLocationParameterName = TEXT("Location");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanFlagParameterName = TEXT("Flag");
	
	//플레이어에 따른 masking 색처리.

private:

	uint8 bScanPostProcessEnabled : 1 = false;
	uint8 bStealthPostProcessEnabled : 1 = false;
	uint8 bDamagedPostProcessEnabled : 1 = false;

	float ScanRangeRange = 0.f;
};
