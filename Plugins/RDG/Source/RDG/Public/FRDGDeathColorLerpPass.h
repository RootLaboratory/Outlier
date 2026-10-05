#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
class FRHITexture;

// 사망 연출 Fade / 비네트 공용 패스. 단색 보간 뒤 비네트 텍스처 rgb × a를 더한다.
// Fade는 보간만, 비네트는 Black 다음에 보간량 0으로 따로 돌아서 리스폰까지 남는다.
// Black은 레이어 합성이 따로 있어서 FRDGDeathBlackPass로 돈다.
class FRDGDeathColorLerpPass
{
public:
	// VignetteTexture가 없거나 VignetteAmount가 0이면 비네트는 건너뛴다.
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FLinearColor& LerpColor,
		float Amount,
		FRHITexture* VignetteTexture,
		float VignetteAmount,
		const TCHAR* PassName,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
