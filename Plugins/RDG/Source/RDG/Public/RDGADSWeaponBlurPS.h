#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGADSWeaponBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGADSWeaponBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FRDGADSWeaponBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HardMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskSampler)
		SHADER_PARAMETER(FVector2f, InvResolution)
		SHADER_PARAMETER(FVector2f, MaskTextureInvResolution)
		SHADER_PARAMETER(FVector2f, CustomDepthTextureInvResolution)
		SHADER_PARAMETER(FVector4f, ViewportUVToOriginalTextureUV)
		SHADER_PARAMETER(FVector4f, ViewportUVToCustomDepthTextureUV)
		SHADER_PARAMETER(FVector4f, ViewportUVToMaskTextureUV)
		SHADER_PARAMETER(float, KernelOffset)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(float, InnerPreserve)
		SHADER_PARAMETER(float, DepthBlurStart)
		SHADER_PARAMETER(float, DepthBlurEnd)
		SHADER_PARAMETER(float, DepthBlurPower)
		SHADER_PARAMETER(float, DepthFocusBias)
		SHADER_PARAMETER(uint32, bUpsample)
		SHADER_PARAMETER(uint32, bCompositeWithOriginal)
		SHADER_PARAMETER(float, FocusDistanceWorld)
		SHADER_PARAMETER(FVector4f, InvDeviceZToWorldZTransform)
		SHADER_PARAMETER(float, MaxCoCRadius)
		SHADER_PARAMETER(uint32, GatherSampleCount)
		SHADER_PARAMETER(float, ReachSoftness)
		SHADER_PARAMETER(uint32, bGather)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
