#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FRDGAdsSightRestorePass
{
public:
	static FRDGTextureRef AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef PostDofColorTexture,
		FRDGTextureRef PreDofColorTexture,
		FRDGTextureRef SightMaskTexture,
		FIntRect ViewRect,
		bool bEnableGpuStats = false);
};
