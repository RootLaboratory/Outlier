#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
class FRHITexture;
struct FDeathBlackParameters;

// 사망 연출 Black. Background lerp 위에 Noise 텍스처 rgb × a를 등장 시점부터 더한다.
// Background와 Noise 텍스처가 모두 필요하다.
class FRDGDeathBlackPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FDeathBlackParameters& Parameters,
		FRHITexture* BackgroundTexture,
		FRHITexture* NoiseTexture,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
