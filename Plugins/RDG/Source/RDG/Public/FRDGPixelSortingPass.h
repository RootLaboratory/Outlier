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

	// View 없이 도는 변형. Slate가 backbuffer를 present 직전에 넘겨주는 지점처럼
	// FSceneView를 얻을 수 없는 곳에서 씀.
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& SceneColor,
		const FPixelSortingParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
