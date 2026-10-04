#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGSplitPrismDefocusPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGSplitPrismDefocusPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGSplitPrismDefocusPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, InputTexelSize)
		SHADER_PARAMETER(float, Defocus)
		SHADER_PARAMETER(float, MaxRadius)
		SHADER_PARAMETER(float, FringeAmount)
		SHADER_PARAMETER(float, RimBias)
		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER(float, MaxMipLevel)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
