#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "FPostProcessStructures.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FRHITexture;
class FSceneView;

// Drone Damage Feedback. Tonemap 이후(UI 제외), 드러난 마스크 안에서 슬라이스 글리치 + 색 더하기.
// 마스크 텍스처는 EDroneDamageMask 순서이고, 비어 있는 슬롯은 검은 텍스처로 대신한다.
class FRDGDroneDamageFeedbackPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		const FDroneDamageFeedbackParameters& Parameters,
		const TStaticArray<FRHITexture*, DroneDamageMaskCount>& MaskTextures,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
