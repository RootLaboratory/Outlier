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
		SHADER_PARAMETER(FVector3f, LerpColor)
		SHADER_PARAMETER(float, Amount)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
