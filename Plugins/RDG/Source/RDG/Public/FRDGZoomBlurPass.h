#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FZoomBlurParameters;

// 화면 중심으로 빨려 들어가는 느낌의 줌 블러. Slate 이후 backbuffer 단계에서 돌기
// 때문에 FSceneView를 받지 않음.
class FRDGZoomBlurPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FZoomBlurParameters& Parameters);
};
