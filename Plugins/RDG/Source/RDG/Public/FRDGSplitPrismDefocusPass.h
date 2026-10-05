#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FSplitPrismDefocusParameters;

// Split Prism 디포커스. Tonemap 앞(MotionBlur 다음) 선형 HDR 씬 컬러를 화면 전체 같은 반경의 원판으로 흐린다.
// 화면 전체 갈라짐 d(t)는 Slate 이후 FRDGSplitPrismPass가 맡고, 여기서는 장면에만 깊이 잔차 r(p)만큼 더 가른다.
// SceneDepthTexture가 없으면 잔차는 끈다.
class FRDGSplitPrismDefocusPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		FRDGTextureRef SceneDepthTexture,
		const FSplitPrismDefocusParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
