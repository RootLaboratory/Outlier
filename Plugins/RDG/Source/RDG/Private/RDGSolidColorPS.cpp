#include "RDGSolidColorPS.h"

#include "FRDGSolidColorPass.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

bool FRDGSolidColorPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGSolidColorPS, "/Plugin/RDG/SolidColor.usf", "MainPS", SF_Pixel);

FScreenPassTexture FRDGSolidColorPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	FScreenPassRenderTarget Output = FScreenPassRenderTarget::CreateFromInput(
		GraphBuilder,
		SceneColor,
		ERenderTargetLoadAction::ENoAction,
		TEXT("RDG.SolidColor.Output"));

	FRDGSolidColorPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGSolidColorPS::FParameters>();
	PassParameters->SolidColor = FVector4f(1.0f, 0.0f, 0.0f, 1.0f);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGSolidColorPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.SolidColor"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
