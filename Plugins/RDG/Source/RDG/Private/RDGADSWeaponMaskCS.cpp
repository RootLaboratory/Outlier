#include "RDGADSWeaponMaskCS.h"

#include "FRDGADSWeaponMaskPass.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SystemTextures.h"

bool FRDGADSWeaponMaskCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGADSWeaponMaskCS, "/Plugin/RDG/ADSWeaponMask.usf", "MainCS", SF_Compute);

static FRDGTextureRef CreateMaskTexture(FRDGBuilder& GraphBuilder, FIntPoint TextureExtent, const TCHAR* Name)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		TextureExtent,
		PF_R32_FLOAT,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	return GraphBuilder.CreateTexture(Desc, Name);
}

FRDGTextureRef FRDGADSWeaponMaskPass::AddBuildMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureSRVRef CustomStencilTexture,
	FIntPoint TextureExtent,
	uint32 WeaponStencilValue)
{
	return AddMaskPass(
		GraphBuilder,
		View,
		CustomStencilTexture,
		nullptr,
		nullptr,
		TextureExtent,
		WeaponStencilValue,
		0.0f,
		0u,
		TEXT("RDG.ADSWeaponMask.Hard"),
		TEXT("RDG.ADSWeaponMask.Build"));
}

FRDGTextureRef FRDGADSWeaponMaskPass::AddDilatePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef InputMask,
	FIntPoint TextureExtent,
	float Radius)
{
	return AddMaskPass(
		GraphBuilder,
		View,
		nullptr,
		InputMask,
		nullptr,
		TextureExtent,
		0u,
		Radius,
		1u,
		TEXT("RDG.ADSWeaponMask.Dilated"),
		TEXT("RDG.ADSWeaponMask.Dilate"));
}

FRDGTextureRef FRDGADSWeaponMaskPass::AddSoftenPass(
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
		InputMask,
		HardMask,
		TextureExtent,
		0u,
		Radius,
		2u,
		TEXT("RDG.ADSWeaponMask.Soft"),
		TEXT("RDG.ADSWeaponMask.Soften"));
}

FRDGTextureRef FRDGADSWeaponMaskPass::AddMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureSRVRef CustomStencilTexture,
	FRDGTextureRef InputMask,
	FRDGTextureRef HardMask,
	FIntPoint TextureExtent,
	uint32 WeaponStencilValue,
	float Radius,
	uint32 PassMode,
	const TCHAR* TextureName,
	const TCHAR* EventName)
{
	if (TextureExtent.X <= 0 || TextureExtent.Y <= 0)
	{
		return nullptr;
	}

	FRDGTextureRef OutputMask = CreateMaskTexture(GraphBuilder, TextureExtent, TextureName);
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	FRDGTextureRef DummyMask = SystemTextures.Black;

	FRDGADSWeaponMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGADSWeaponMaskCS::FParameters>();
	PassParameters->CustomStencilTexture = CustomStencilTexture ? CustomStencilTexture : SystemTextures.StencilDummySRV;
	PassParameters->InputMask = InputMask ? InputMask : DummyMask;
	PassParameters->HardMask = HardMask ? HardMask : DummyMask;
	PassParameters->OutMask = GraphBuilder.CreateUAV(OutputMask);
	PassParameters->TextureExtent = TextureExtent;
	PassParameters->SourceViewRectMin = View.UnscaledViewRect.Min;
	PassParameters->WeaponStencilValue = WeaponStencilValue;
	PassParameters->Radius = FMath::Max(0.0f, Radius);
	PassParameters->PassMode = PassMode;

	TShaderMapRef<FRDGADSWeaponMaskCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", EventName),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TextureExtent, FIntPoint(8, 8)));

	return OutputMask;
}
