#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FMotionBlurParameters;

class FRDGMotionBlurPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FMotionBlurParameters& Parameters);
};

//RDG 리소스/입출력 관리, Texture 생성, graph 연결, Shader 실행 orchestration
