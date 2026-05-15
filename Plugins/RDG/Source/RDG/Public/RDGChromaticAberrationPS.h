#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRGDChromaticAberrationPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRGDChromaticAberrationPS);
	SHADER_USE_PARAMETER_STRUCT(FRGDChromaticAberrationPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)

		SHADER_PARAMETER(uint32, bEnabled)
		SHADER_PARAMETER(float, StartOffset)
		SHADER_PARAMETER(FVector2f, CAScale)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
