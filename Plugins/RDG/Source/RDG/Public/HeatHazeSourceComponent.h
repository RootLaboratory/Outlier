// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeatHazeSourceComponent.generated.h"

UENUM(BlueprintType)
enum class EHeatHazeSourceShape : uint8
{
	Sphere,
	Box,
	Capsule,
	ScreenAlignedQuad,
	MeshBounds,
	ComponentBounds
};
UENUM(BlueprintType)
enum class EHeatHazeBlendMode : uint8
{
	Additive,
	Max,
	AlphaBlend
};

struct FHeatHazeSourceData
{
	FTransform3f Transform;
	FVector2f Size = FVector2f(100.0f, 100.0f);
	float Strength = 1.0f;
	float MaxPixelOffset = 24.0f;
	float EdgeSoftness = 0.25f;
	float FalloffExponent = 2.0f;
	float NoiseScale = 1.0f;
	float NoiseSpeed = 1.0f;
	FVector2f NoiseDirection = FVector2f(0.0f, 1.0f);
	float DepthFadeDistance = 50.0f;
	uint32 bUseDepthOcclusion = 1;
};

UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class RDG_API UHeatHazeSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UHeatHazeSourceComponent();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
public:
	bool BuildSourceData(FHeatHazeSourceData& OutData) const;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze")
	EHeatHazeSourceShape Shape = EHeatHazeSourceShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze")
	EHeatHazeBlendMode BlendMode = EHeatHazeBlendMode::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Shape")
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Shape")
	FVector BoxExtent = FVector(100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Shape")
	float CapsuleHalfHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Shape")
	float BoundsPadding = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Distortion")
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Distortion")
	float MaxPixelOffset = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Distortion")
	float EdgeSoftness = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Distortion")
	float FalloffExponent = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Depth")
	bool bUseDepthOcclusion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Depth")
	float DepthFadeDistance = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Noise")
	TObjectPtr<UTexture2D> NoiseTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Noise")
	float NoiseScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Noise")
	float NoiseSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Noise")
	FVector2D NoiseDirection = FVector2D(0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Material")
	TObjectPtr<UMaterialInterface> DistortionWriterMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Distance")
	float MaxDrawDistance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat Haze|Debug")
	bool bDrawDebugBounds = false;
	
};
