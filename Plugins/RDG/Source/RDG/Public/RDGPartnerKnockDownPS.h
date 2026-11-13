#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// 마스크 텍스처 + 회복 진행도 → 글리치 가중치(R8).
class FRDGPartnerKnockDownMaskPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGPartnerKnockDownMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGPartnerKnockDownMaskPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskSampler)
		SHADER_PARAMETER(float, MaskBlackPoint)
		SHADER_PARAMETER(float, MaskWhitePoint)
		SHADER_PARAMETER(float, MaskGamma)
		SHADER_PARAMETER(float, Progress)
		SHADER_PARAMETER(float, RecoverSoftness)
		SHADER_PARAMETER(int32, bMaskInvert)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// 가중치를 곱한 슬라이스 글리치 + 렌즈 CA + DamageHud 합성.
class FRDGPartnerKnockDownPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGPartnerKnockDownPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGPartnerKnockDownPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, ViewRectMinUV)
		SHADER_PARAMETER(FVector2f, ViewRectMaxUV)
		SHADER_PARAMETER(FVector2f, ViewportUVOrigin)
		SHADER_PARAMETER(FVector2f, ViewportUVSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeightTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeightSampler)
		SHADER_PARAMETER(int32, bMaskVisualize)
		SHADER_PARAMETER_TEXTURE(Texture2D, DamageHudTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DamageHudSampler)
		SHADER_PARAMETER(FVector3f, DamageHudTint)
		SHADER_PARAMETER(float, DamageHudOpacity)
		SHADER_PARAMETER(float, DamageHudExponent)
		SHADER_PARAMETER(float, DamageHudIntensity)
		SHADER_PARAMETER(float, DamageHudDarken)
		SHADER_PARAMETER(float, DamageHudBreath)
		SHADER_PARAMETER(int32, DamageHudBlendMode)
		SHADER_PARAMETER(int32, bEncodeDamageHudToSRGB)
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
		SHADER_PARAMETER(float, ChromaticStartOffset)
		SHADER_PARAMETER(FVector2f, ChromaticScale)
		SHADER_PARAMETER(int32, bChromaticGlitchOnly)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
