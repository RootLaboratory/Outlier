#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;

class FRDGExplosionVolumeVisualizePass
{
public:
	static bool IsEnabled();

	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FRDGTextureRef& CachedVolume,
		const FScreenPassRenderTarget& OverrideOutput
	);
};
