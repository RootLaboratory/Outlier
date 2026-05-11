#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSceneView;

struct FExplosionVolumeParameters
{
	FVector3f ExplosionCenter = FVector3f(1000.0f, 0.0f, 100.0f);
	float ExplosionRadius = 2000.0f;
	float ExplosionForce = 500.0f;
};

class FRDGExplosionVolumePass
{
public:
	static FRDGTextureRef AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneDepthTexture,
		const FExplosionVolumeParameters& Parameters);
};
