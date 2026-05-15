#pragma once
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FDatamoshingParameters;

class FRDGDatamoshingPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FDatamoshingParameters& Parameters,
		TRefCountPtr<IPooledRenderTarget>& InOutHistoryRT,
		const FScreenPassRenderTarget& OverrideOutput
	);
};
