#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;

// 사망 연출 Fade / Black이 같이 쓰는 색 보간 패스. 셰이더는 하나지만 PassName으로
// 그래프에서 서로 다른 패스로 보이게 한다.
class FRDGDeathColorLerpPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FLinearColor& LerpColor,
		float Amount,
		const TCHAR* PassName,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
