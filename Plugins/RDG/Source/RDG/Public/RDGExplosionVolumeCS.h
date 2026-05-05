#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

class FExplosionVolumeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FExplosionVolumeCS);
	SHADER_USE_PARAMETER_STRUCT(FExplosionVolumeCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVolume)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER(FVector3f, ExplosionCenter)
		SHADER_PARAMETER(float, ExplosionRadius)
		SHADER_PARAMETER(float, ExplosionForce)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	END_SHADER_PARAMETER_STRUCT()
};
