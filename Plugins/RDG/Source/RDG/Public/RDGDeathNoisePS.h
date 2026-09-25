#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDeathNoisePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDeathNoisePS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDeathNoisePS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, ViewRectMinUV)
		SHADER_PARAMETER(FVector2f, ViewRectMaxUV)
		SHADER_PARAMETER(FVector2f, ViewportUVOrigin)
		SHADER_PARAMETER(FVector2f, ViewportUVSize)
		SHADER_PARAMETER(float, Time)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(float, Seed)
		SHADER_PARAMETER(float, SliceRows)
		SHADER_PARAMETER(float, SliceSplitChance)
		SHADER_PARAMETER(float, GlitchRate)
		SHADER_PARAMETER(float, GlitchStrength)
		SHADER_PARAMETER(float, GlitchThreshold)
		SHADER_PARAMETER(float, GlitchGlow)
		SHADER_PARAMETER(float, BurstChance)
		SHADER_PARAMETER(float, BurstStrength)
		SHADER_PARAMETER(float, BurstThreshold)
		SHADER_PARAMETER(FVector3f, Tint)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
