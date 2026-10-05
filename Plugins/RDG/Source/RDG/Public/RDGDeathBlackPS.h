#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDeathBlackPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDeathBlackPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDeathBlackPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, BackgroundTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, NoiseTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LayerSampler)
		SHADER_PARAMETER(float, BackgroundAmount)
		SHADER_PARAMETER(float, NoiseAmount)
		SHADER_PARAMETER(int32, bEncodeBackgroundToSRGB)
		SHADER_PARAMETER(int32, bEncodeNoiseToSRGB)
		SHADER_PARAMETER(FVector2f, InputUVMin)
		SHADER_PARAMETER(FVector2f, InputUVSizeInverse)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
