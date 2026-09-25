#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FDeathChromaticAberrationParameters;

class FRDGDeathChromaticAberrationPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FDeathChromaticAberrationParameters& Parameters);
};
