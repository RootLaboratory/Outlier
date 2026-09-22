// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/RetainerBox.h"
#include "CurvedRetainerBox.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Retainer Box helper for cylindrical or spherical HUD effect materials. */
UCLASS(BlueprintType, Blueprintable)
class TAGDRIVENUI_API UCurvedRetainerBox : public URetainerBox
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void RefreshCurvedMaterial();

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetCurvatureDegrees(float InDegrees);

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetCurvatureEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetSphericalProjectionDegrees(
		float InHorizontalArcDegrees,
		float InVerticalArcDegrees,
		float InSourceHFovDegrees,
		float InSourceVFovDegrees);

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetWarpAmount(float InWarpAmount);

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetTextureScale(float InTextureScale);

	UFUNCTION(BlueprintCallable, Category = "Curved Retainer")
	void SetViewportRegion(FVector2D InOffset, FVector2D InScale);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	TObjectPtr<UMaterialInterface> CurvedEffectMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName CurvatureParameterName = TEXT("CurvatureDegrees");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName TextureParameterName = TEXT("Texture");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName HorizontalArcParameterName = TEXT("HorizontalArcDegrees");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName VerticalArcParameterName = TEXT("VerticalArcDegrees");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName SourceHFovParameterName = TEXT("SourceHFovDegrees");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName SourceVFovParameterName = TEXT("SourceVFovDegrees");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName WarpAmountParameterName = TEXT("WarpAmount");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName TextureScaleParameterName = TEXT("TextureScale");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	FName ViewportRegionParameterName = TEXT("ViewportRegion");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float CurvatureDegrees = 15.0f;

	/** Angular width of the spherical output patch. Must stay below 180 degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "1.0", ClampMax = "175.0", Units = "deg"))
	float HorizontalArcDegrees = 90.0f;

	/** Angular height of the spherical output patch. Must stay below 180 degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "1.0", ClampMax = "175.0", Units = "deg"))
	float VerticalArcDegrees = 60.0f;

	/** Horizontal angular coverage of the source UI projection plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "1.0", ClampMax = "175.0", Units = "deg"))
	float SourceHFovDegrees = 100.0f;

	/** Vertical angular coverage of the source UI projection plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "1.0", ClampMax = "175.0", Units = "deg"))
	float SourceVFovDegrees = 70.0f;

	/** Blend between the original UI and the spherical projection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarpAmount = 1.0f;

	/** Scale applied to the rendered UI texture after spherical projection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Spherical Projection", meta = (ClampMin = "0.01"))
	float TextureScale = 1.0f;

	/** Normalized top-left position of this retainer within the full HUD design space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Viewport Region")
	FVector2D ViewportRegionOffset = FVector2D::ZeroVector;

	/** Normalized size of this retainer within the full HUD design space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Viewport Region")
	FVector2D ViewportRegionScale = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Retainer|Material")
	uint8 bEnableCurvature : 1 = true;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CurvedEffectMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedCurvedEffectMaterial;
};
