#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

class FSceneView;
struct FADSBlurParameters;

class FRDGADSWeaponBlurPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		FRDGTextureRef MaskTexture,
		FRDGTextureRef HardMaskTexture,
		FRDGTextureRef CustomDepthTexture,
		const FADSBlurParameters& Parameters,
		const FScreenPassRenderTarget& OverrideOutput = FScreenPassRenderTarget());
};
