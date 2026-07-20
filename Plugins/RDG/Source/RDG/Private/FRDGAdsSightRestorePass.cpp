#include "FRDGAdsSightRestorePass.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphEvent.h"

DECLARE_GPU_STAT_NAMED(OutlierADSSightRestore, TEXT("Outlier ADS Sight Restore"));

namespace
{
	constexpr int32 kSightRestoreGroupSize = 8;
}

class FOutlierAdsSightRestoreCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutlierAdsSightRestoreCS);
	SHADER_USE_PARAMETER_STRUCT(FOutlierAdsSightRestoreCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PostDofColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreDofColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SightMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SightMaskUintTexture)
		SHADER_PARAMETER(FIntPoint, ViewMin)
		SHADER_PARAMETER(FIntPoint, ViewMax)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, OutColor)
	END_SHADER_PARAMETER_STRUCT()

	class FUintMask : SHADER_PERMUTATION_BOOL("SIGHT_MASK_UINT");
	using FPermutationDomain = TShaderPermutationDomain<FUintMask>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), kSightRestoreGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOutlierAdsSightRestoreCS, "/Plugin/RDG/OutlierAdsSightRestore.usf", "MainCS", SF_Compute);

FRDGTextureRef FRDGAdsSightRestorePass::AddPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef PostDofColorTexture,
	FRDGTextureRef PreDofColorTexture,
	FRDGTextureRef SightMaskTexture,
	FIntRect ViewRect,
	bool bEnableGpuStats)
{
	FRDGTextureDesc OutDesc = PostDofColorTexture->Desc;
	OutDesc.Flags |= TexCreate_UAV;

	FRDGTextureRef OutColor = GraphBuilder.CreateTexture(OutDesc, TEXT("Outlier.AdsSightRestoreColor"));

	FOutlierAdsSightRestoreCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOutlierAdsSightRestoreCS::FParameters>();
	const bool bUintMask = SightMaskTexture->Desc.Format == PF_R8_UINT;
	Parameters->PostDofColorTexture = PostDofColorTexture;
	Parameters->PreDofColorTexture = PreDofColorTexture;
	Parameters->SightMaskTexture = bUintMask ? nullptr : SightMaskTexture;
	Parameters->SightMaskUintTexture = bUintMask ? SightMaskTexture : nullptr;
	Parameters->ViewMin = ViewRect.Min;
	Parameters->ViewMax = ViewRect.Max;
	Parameters->OutColor = GraphBuilder.CreateUAV(OutColor);

	FOutlierAdsSightRestoreCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FOutlierAdsSightRestoreCS::FUintMask>(bUintMask);
	TShaderMapRef<FOutlierAdsSightRestoreCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	ClearUnusedGraphResources(ComputeShader, Parameters);

	auto AddRestorePass = [&]()
	{
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("OutlierAdsSightRestore %dx%d", ViewRect.Width(), ViewRect.Height()),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(ViewRect.Size(), FIntPoint(kSightRestoreGroupSize)));
	};

	if (bEnableGpuStats)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, OutlierADSSightRestore, "Outlier ADS Sight Restore");
		AddRestorePass();
	}
	else
	{
		AddRestorePass();
	}

	return OutColor;
}
