#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRDGExplosionVolumeVisualizePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRDGExplosionVolumeVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FRDGExplosionVolumeVisualizePS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VolumeTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
		SHADER_PARAMETER(FVector4f, VisualizeSettings)
		SHADER_PARAMETER(FVector4f, TextureSettings)
		SHADER_PARAMETER(FVector4f, ViewRectUV)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
