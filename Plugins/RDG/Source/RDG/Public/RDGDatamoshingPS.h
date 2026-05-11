#pragma once
#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDatamoshingPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDatamoshingPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDatamoshingPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentFrameTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryFrameTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, GlobalSampler)
		SHADER_PARAMETER(float, Progress)
		SHADER_PARAMETER(FVector2f, InvResolution)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
