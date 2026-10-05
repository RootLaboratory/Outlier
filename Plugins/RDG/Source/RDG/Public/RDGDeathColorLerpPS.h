#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDeathColorLerpPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDeathColorLerpPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDeathColorLerpPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, VignetteTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VignetteSampler)
		SHADER_PARAMETER(FVector3f, LerpColor)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, VignetteAmount)
		SHADER_PARAMETER(int32, bEncodeVignetteToSRGB)
		SHADER_PARAMETER(FVector2f, InputUVMin)
		SHADER_PARAMETER(FVector2f, InputUVSizeInverse)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
