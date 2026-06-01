#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSceneView;

namespace RDGExplosionVolume
{
	inline constexpr int32 VolumeSize = 64;
	inline constexpr float MaxDepth = 500.0f;
}

class FRDGExplosionVolumePass
{
public:
	static bool IsEnabled();

	static FRDGTextureRef AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneDepthTexture);
};
