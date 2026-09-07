#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FOverlayParameters;

class FRDGOverlayPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FOverlayParameters& Parameters);
};
