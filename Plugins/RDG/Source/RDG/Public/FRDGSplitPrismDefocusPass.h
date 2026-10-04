#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FSplitPrismDefocusParameters;

// Split Prism 디포커스. Tonemap 앞(MotionBlur 다음) 선형 HDR 씬 컬러를 화면 전체 같은 반경의 원판으로 흐린다.
// 갈라짐은 Slate 이후 FRDGSplitPrismPass가 맡는다.
class FRDGSplitPrismDefocusPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FSplitPrismDefocusParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
