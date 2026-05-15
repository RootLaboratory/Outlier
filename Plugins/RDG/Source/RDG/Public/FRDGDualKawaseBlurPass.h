#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FDualKawaseBlurParameters;

class FRDGDualKawaseBlurPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FDualKawaseBlurParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
