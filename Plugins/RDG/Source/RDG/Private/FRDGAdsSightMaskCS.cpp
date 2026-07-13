#include "FRDGAdsSightMaskPass.h"

#include "FPostProcessStructures.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "SystemTextures.h"

class FOutlierAdsSightMaskCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutlierAdsSightMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FOutlierAdsSightMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HardMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutMask)
		SHADER_PARAMETER(FIntPoint, TextureExtent)
		SHADER_PARAMETER(FIntPoint, SourceViewRectMin)
		SHADER_PARAMETER(uint32, WeaponStencilValue)
		SHADER_PARAMETER(FVector4f, InvDeviceZToWorldZTransform)
		SHADER_PARAMETER(float, FocusDistanceWorld)
		SHADER_PARAMETER(float, SightDistanceThreshold)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(uint32, PassMode)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOutlierAdsSightMaskCS, "/Plugin/RDG/OutlierAdsSightMask.usf", "MainCS", SF_Compute);

static FRDGTextureRef CreateSightMaskTexture(FRDGBuilder& GraphBuilder, FIntPoint TextureExtent, const TCHAR* Name)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		TextureExtent,
		PF_R32_FLOAT,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(Desc, Name);
}

FRDGTextureRef FRDGAdsSightMaskPass::AddBuildMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureSRVRef CustomStencilTexture,
	FRDGTextureRef CustomDepthTexture,
	FIntPoint TextureExtent,
	const FADSBlurParameters& Parameters)
{
	return AddMaskPass(
		GraphBuilder,
		View,
		CustomStencilTexture,
		CustomDepthTexture,
		nullptr,
		nullptr,
		TextureExtent,
		static_cast<uint32>(Parameters.WeaponStencilValue),
		Parameters.FocusDistanceWorld,
		Parameters.SightDistanceThreshold,
		0.0f,
		0u,
		TEXT("Outlier.AdsSightMask.Hard"),
		TEXT("Outlier.AdsSightMask.Build"));
}

FRDGTextureRef FRDGAdsSightMaskPass::AddDilatePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InputMask,
	FIntPoint TextureExtent,
	float Radius)
{
	FRDGTextureRef HorizontalMask = AddMaskPass(
		GraphBuilder,
		View,
		nullptr,
		nullptr,
		InputMask,
		nullptr,
		TextureExtent,
		0u,
		0.0f,
		0.0f,
		Radius,
		1u,
		TEXT("Outlier.AdsSightMask.DilateH"),
		TEXT("Outlier.AdsSightMask.DilateH"));

	return AddMaskPass(
		GraphBuilder,
		View,
		nullptr,
		nullptr,
		HorizontalMask,
		nullptr,
		TextureExtent,
		0u,
		0.0f,
		0.0f,
		Radius,
		2u,
		TEXT("Outlier.AdsSightMask.Dilated"),
		TEXT("Outlier.AdsSightMask.DilateV"));
}

FRDGTextureRef FRDGAdsSightMaskPass::AddSoftenPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InputMask,
	FRDGTextureRef HardMask,
	FIntPoint TextureExtent,
	float Radius)
{
	return AddMaskPass(
		GraphBuilder,
		View,
		nullptr,
		nullptr,
		InputMask,
		HardMask,
		TextureExtent,
		0u,
		0.0f,
		0.0f,
		Radius,
		3u,
		TEXT("Outlier.AdsSightMask.Soft"),
		TEXT("Outlier.AdsSightMask.Soften"));
}

FRDGTextureRef FRDGAdsSightMaskPass::AddMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureSRVRef CustomStencilTexture,
	FRDGTextureRef CustomDepthTexture,
	FRDGTextureRef InputMask,
	FRDGTextureRef HardMask,
	FIntPoint TextureExtent,
	uint32 WeaponStencilValue,
	float FocusDistanceWorld,
	float SightDistanceThreshold,
	float Radius,
	uint32 PassMode,
	const TCHAR* TextureName,
	const TCHAR* EventName)
{
	if (TextureExtent.X <= 0 || TextureExtent.Y <= 0)
	{
		return nullptr;
	}

	FRDGTextureRef OutputMask = CreateSightMaskTexture(GraphBuilder, TextureExtent, TextureName);
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	FRDGTextureRef DummyMask = SystemTextures.Black;

	FOutlierAdsSightMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutlierAdsSightMaskCS::FParameters>();
	PassParameters->CustomStencilTexture = CustomStencilTexture ? CustomStencilTexture : SystemTextures.StencilDummySRV;
	PassParameters->CustomDepthTexture = CustomDepthTexture ? CustomDepthTexture : SystemTextures.Black;
	PassParameters->InputMask = InputMask ? InputMask : DummyMask;
	PassParameters->HardMask = HardMask ? HardMask : DummyMask;
	PassParameters->OutMask = GraphBuilder.CreateUAV(OutputMask);
	PassParameters->TextureExtent = TextureExtent;
	PassParameters->SourceViewRectMin = View.UnscaledViewRect.Min;
	PassParameters->WeaponStencilValue = WeaponStencilValue;
	PassParameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);
	PassParameters->FocusDistanceWorld = FocusDistanceWorld;
	PassParameters->SightDistanceThreshold = SightDistanceThreshold;
	PassParameters->Radius = FMath::Max(0.0f, Radius);
	PassParameters->PassMode = PassMode;

	TShaderMapRef<FOutlierAdsSightMaskCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", EventName),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TextureExtent, FIntPoint(8, 8)));

	return OutputMask;
}
