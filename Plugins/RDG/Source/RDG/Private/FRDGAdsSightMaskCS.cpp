#include "FRDGAdsSightMaskPass.h"

#include "FPostProcessStructures.h"
#include "FXRenderingUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "SystemTextures.h"

class FOutlierAdsSightIntegerMaskCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutlierAdsSightIntegerMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FOutlierAdsSightIntegerMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, InputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutMask)
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

IMPLEMENT_GLOBAL_SHADER(FOutlierAdsSightIntegerMaskCS, "/Plugin/RDG/OutlierAdsSightMask.usf", "IntegerMaskMainCS", SF_Compute);

class FOutlierAdsSightSoftenHorizontalCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutlierAdsSightSoftenHorizontalCS);
	SHADER_USE_PARAMETER_STRUCT(FOutlierAdsSightSoftenHorizontalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, InputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSoftMask)
		SHADER_PARAMETER(FIntPoint, TextureExtent)
		SHADER_PARAMETER(float, Radius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOutlierAdsSightSoftenHorizontalCS, "/Plugin/RDG/OutlierAdsSightMask.usf", "SoftenHorizontalMainCS", SF_Compute);

class FOutlierAdsSightSoftenVerticalCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutlierAdsSightSoftenVerticalCS);
	SHADER_USE_PARAMETER_STRUCT(FOutlierAdsSightSoftenVerticalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputSoftMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HardMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSoftMask)
		SHADER_PARAMETER(FIntPoint, TextureExtent)
		SHADER_PARAMETER(float, Radius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOutlierAdsSightSoftenVerticalCS, "/Plugin/RDG/OutlierAdsSightMask.usf", "SoftenVerticalMainCS", SF_Compute);

static FRDGTextureRef CreateSightMaskTexture(
	FRDGBuilder& GraphBuilder,
	FIntPoint TextureExtent,
	EPixelFormat Format,
	const TCHAR* Name)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		TextureExtent,
		Format,
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
	return AddIntegerMaskPass(
		GraphBuilder,
		View,
		CustomStencilTexture,
		CustomDepthTexture,
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
	FRDGTextureRef HorizontalMask = AddIntegerMaskPass(
		GraphBuilder,
		View,
		nullptr,
		nullptr,
		InputMask,
		TextureExtent,
		0u,
		0.0f,
		0.0f,
		Radius,
		1u,
		TEXT("Outlier.AdsSightMask.DilateH"),
		TEXT("Outlier.AdsSightMask.DilateH"));

	return AddIntegerMaskPass(
		GraphBuilder,
		View,
		nullptr,
		nullptr,
		HorizontalMask,
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
	return AddSoftenMaskPass(
		GraphBuilder,
		View,
		InputMask,
		HardMask,
		TextureExtent,
		Radius,
		TEXT("Outlier.AdsSightMask.Soft"),
		TEXT("Outlier.AdsSightMask.Soften"));
}

FRDGTextureRef FRDGAdsSightMaskPass::AddIntegerMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureSRVRef CustomStencilTexture,
	FRDGTextureRef CustomDepthTexture,
	FRDGTextureRef InputMask,
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

	FRDGTextureRef OutputMask = CreateSightMaskTexture(GraphBuilder, TextureExtent, PF_R8_UINT, TextureName);
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	FRDGTextureRef DummyMask = GSystemTextures.GetZeroUIntDummy(GraphBuilder);

	FOutlierAdsSightIntegerMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutlierAdsSightIntegerMaskCS::FParameters>();
	PassParameters->CustomStencilTexture = CustomStencilTexture ? CustomStencilTexture : SystemTextures.StencilDummySRV;
	PassParameters->CustomDepthTexture = CustomDepthTexture ? CustomDepthTexture : SystemTextures.Black;
	PassParameters->InputMask = InputMask ? InputMask : DummyMask;
	PassParameters->OutMask = GraphBuilder.CreateUAV(OutputMask);
	PassParameters->TextureExtent = TextureExtent;
	PassParameters->SourceViewRectMin = UE::FXRenderingUtils::GetRawViewRectUnsafe(View).Min;
	PassParameters->WeaponStencilValue = WeaponStencilValue;
	PassParameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);
	PassParameters->FocusDistanceWorld = FocusDistanceWorld;
	PassParameters->SightDistanceThreshold = SightDistanceThreshold;
	PassParameters->Radius = FMath::Max(0.0f, Radius);
	PassParameters->PassMode = PassMode;

	TShaderMapRef<FOutlierAdsSightIntegerMaskCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s %dx%d", EventName, TextureExtent.X, TextureExtent.Y),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TextureExtent, FIntPoint(8, 8)));

	return OutputMask;
}

FRDGTextureRef FRDGAdsSightMaskPass::AddSoftenMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InputMask,
	FRDGTextureRef HardMask,
	FIntPoint TextureExtent,
	float Radius,
	const TCHAR* TextureName,
	const TCHAR* EventName)
{
	if (TextureExtent.X <= 0 || TextureExtent.Y <= 0 || !InputMask || !HardMask)
	{
		return nullptr;
	}

	const float ClampedRadius = FMath::Max(0.0f, Radius);
	FRDGTextureRef HorizontalMask = CreateSightMaskTexture(
		GraphBuilder,
		TextureExtent,
		PF_R16F,
		TEXT("Outlier.AdsSightMask.SoftH"));

	FOutlierAdsSightSoftenHorizontalCS::FParameters* HorizontalParameters = GraphBuilder.AllocParameters<FOutlierAdsSightSoftenHorizontalCS::FParameters>();
	HorizontalParameters->InputMask = InputMask;
	HorizontalParameters->OutSoftMask = GraphBuilder.CreateUAV(HorizontalMask);
	HorizontalParameters->TextureExtent = TextureExtent;
	HorizontalParameters->Radius = ClampedRadius;

	TShaderMapRef<FOutlierAdsSightSoftenHorizontalCS> HorizontalShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%sH %dx%d", EventName, TextureExtent.X, TextureExtent.Y),
		HorizontalShader,
		HorizontalParameters,
		FComputeShaderUtils::GetGroupCount(TextureExtent, FIntPoint(8, 8)));

	FRDGTextureRef OutputMask = CreateSightMaskTexture(GraphBuilder, TextureExtent, PF_R8, TextureName);

	FOutlierAdsSightSoftenVerticalCS::FParameters* VerticalParameters = GraphBuilder.AllocParameters<FOutlierAdsSightSoftenVerticalCS::FParameters>();
	VerticalParameters->InputSoftMask = HorizontalMask;
	VerticalParameters->HardMask = HardMask;
	VerticalParameters->OutSoftMask = GraphBuilder.CreateUAV(OutputMask);
	VerticalParameters->TextureExtent = TextureExtent;
	VerticalParameters->Radius = ClampedRadius;

	TShaderMapRef<FOutlierAdsSightSoftenVerticalCS> VerticalShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%sV %dx%d", EventName, TextureExtent.X, TextureExtent.Y),
		VerticalShader,
		VerticalParameters,
		FComputeShaderUtils::GetGroupCount(TextureExtent, FIntPoint(8, 8)));

	return OutputMask;
}
