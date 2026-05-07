#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGHeatHazeCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGHeatHazeCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FRDGHeatHazeCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RefractionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, RefractionSampler)
		SHADER_PARAMETER(FVector2f, FieldExtent)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
