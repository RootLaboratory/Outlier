#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FPixelSortingParameters;

class FRDGPixelSortingPass
{
public:
	static constexpr int32 MaxLineLength = 2048;

	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FPixelSortingParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
