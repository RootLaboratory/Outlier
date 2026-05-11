#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FHeatHazeSourceData;

class FRDGHeatHazePass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		TConstArrayView<FHeatHazeSourceData> Sources,
		const FScreenPassRenderTarget& OverrideOutput
		);
};
