#include "FRDGExplosionVolumePass.h"

#include "RDGExplosionVolumeCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"

namespace
{
constexpr int32 ExplosionVolumeSize = 64;
constexpr int32 ExplosionVolumeThreadGroupSize = 8;
}

FRDGTextureRef FRDGExplosionVolumePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneDepthTexture,
	const FExplosionVolumeParameters& Parameters)
{
	if (!SceneDepthTexture || Parameters.ExplosionRadius <= 0.0f || Parameters.ExplosionForce == 0.0f)
	{
		return nullptr;
	}

	FRDGTextureDesc VolumeDesc = FRDGTextureDesc::Create3D(
		FIntVector(ExplosionVolumeSize, ExplosionVolumeSize, ExplosionVolumeSize),
		PF_FloatRGBA,
		FClearValueBinding::Transparent,
		TexCreate_UAV | TexCreate_ShaderResource);

	FRDGTextureRef VelocityVolume = GraphBuilder.CreateTexture(VolumeDesc, TEXT("RDG.ExplosionVelocityVolume"));

	FExplosionVolumeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FExplosionVolumeCS::FParameters>();
	PassParameters->OutVolume = GraphBuilder.CreateUAV(VelocityVolume);
	PassParameters->SceneDepthTexture = SceneDepthTexture;
	PassParameters->PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ExplosionCenter = Parameters.ExplosionCenter;
	PassParameters->ExplosionRadius = Parameters.ExplosionRadius;
	PassParameters->ExplosionForce = Parameters.ExplosionForce;
	PassParameters->View = View.ViewUniformBuffer;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FExplosionVolumeCS> ComputeShader(ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.ExplosionVolume"),
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		ComputeShader,
		PassParameters,
		FIntVector(
			ExplosionVolumeSize / ExplosionVolumeThreadGroupSize,
			ExplosionVolumeSize / ExplosionVolumeThreadGroupSize,
			ExplosionVolumeSize / ExplosionVolumeThreadGroupSize));

	return VelocityVolume;
}
