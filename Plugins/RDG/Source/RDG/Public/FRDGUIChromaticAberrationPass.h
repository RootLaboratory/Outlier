#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ScreenPass.h"

struct FUIChromaticAberrationParameters;

class FRDGUIChromaticAberrationPass
{
public:
	static FScreenPassTexture AddPass(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Input,
		const FUIChromaticAberrationParameters& Parameters);

	static FVector2f BuildChromaticScale(float Intensity, float StartOffset);


};
