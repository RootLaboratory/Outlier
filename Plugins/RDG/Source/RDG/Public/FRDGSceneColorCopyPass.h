#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;

class FRDGSceneColorCopyPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());

	// View 없이 도는 변형. Slate의 backbuffer-ready 훅처럼 FSceneView를 얻을 수 없는
	// 지점에서 씀. 포맷/스위즐이 다른 타깃으로도 안전하게 옮겨줌(단순 복사와 달리).
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& SceneColor,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
