#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDualKawaseBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDualKawaseBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDualKawaseBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, InvResolution)
		SHADER_PARAMETER(float, KernelOffset)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(uint32, bUpsample)
		SHADER_PARAMETER(uint32, bCompositeWithOriginal)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
