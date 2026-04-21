#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGMotionBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGMotionBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGMotionBlurPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(float, VelocityScale)
		SHADER_PARAMETER(uint32, bEnabled)
		SHADER_PARAMETER(FVector2f, ViewRectMinUV)
		SHADER_PARAMETER(FVector2f, ViewRectMaxUV)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
