#include "FRDGExplosionVolumePass.h"

#include "RDGExplosionVolumeCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"

namespace
{
constexpr int32 ExplosionVolumeThreadGroupSize = 8;
}

FRDGTextureRef FRDGExplosionVolumePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneDepthTexture)
{
	if (!SceneDepthTexture)
	{
		return nullptr;
	}

	FRDGTextureDesc VolumeDesc = FRDGTextureDesc::Create3D(
		FIntVector(RDGExplosionVolume::VolumeSize, RDGExplosionVolume::VolumeSize, RDGExplosionVolume::VolumeSize),
		PF_FloatRGBA,
		FClearValueBinding::Transparent,
		TexCreate_UAV | TexCreate_ShaderResource);

	FRDGTextureRef VelocityVolume = GraphBuilder.CreateTexture(VolumeDesc, TEXT("RDG.ExplosionVelocityVolume"));

	FExplosionVolumeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FExplosionVolumeCS::FParameters>();
	PassParameters->OutVolume = GraphBuilder.CreateUAV(VelocityVolume);
	PassParameters->SceneDepthTexture = SceneDepthTexture;
	PassParameters->PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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
			RDGExplosionVolume::VolumeSize / ExplosionVolumeThreadGroupSize,
			RDGExplosionVolume::VolumeSize / ExplosionVolumeThreadGroupSize,
			RDGExplosionVolume::VolumeSize / ExplosionVolumeThreadGroupSize));

	return VelocityVolume;
}
