#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FDeathNoiseParameters;

class FRDGDeathNoisePass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FDeathNoiseParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
