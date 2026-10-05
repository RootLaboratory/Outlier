#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGDroneDamageFeedbackPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGDroneDamageFeedbackPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGDroneDamageFeedbackPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SortedTexture)
		SHADER_PARAMETER(FVector2f, SortedUVOrigin)
		SHADER_PARAMETER(FVector2f, SortedUVSize)
		SHADER_PARAMETER(FVector2f, SortedUVMin)
		SHADER_PARAMETER(FVector2f, SortedUVMax)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture0)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture1)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture2)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture3)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskSampler)
		SHADER_PARAMETER(FVector4f, MaskWeights)
		SHADER_PARAMETER(FVector4f, MaskSRGB)
		SHADER_PARAMETER(FVector2f, ViewRectMinUV)
		SHADER_PARAMETER(FVector2f, ViewRectMaxUV)
		SHADER_PARAMETER(FVector2f, ViewportUVOrigin)
		SHADER_PARAMETER(FVector2f, ViewportUVSize)
		SHADER_PARAMETER(uint32, bSliceGlitch)
		SHADER_PARAMETER(uint32, bPixelSort)
		SHADER_PARAMETER(uint32, bShiftToBlack)
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
		SHADER_PARAMETER(FVector2f, ShiftOffset)
		SHADER_PARAMETER(float, ShiftJitter)
		SHADER_PARAMETER(float, MaskGlow)
		SHADER_PARAMETER(FVector3f, Tint)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
