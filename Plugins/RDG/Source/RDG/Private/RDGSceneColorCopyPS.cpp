#include "RDGSceneColorCopyPS.h"

#include "FRDGSceneColorCopyPass.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

bool FRDGSceneColorCopyPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGSceneColorCopyPS, "/Plugin/RDG/SceneColorCopy.usf", "MainPS", SF_Pixel);

FScreenPassTexture FRDGSceneColorCopyPass::AddPass(
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
		TEXT("RDG.SceneColorCopy.Output"));

	FRDGSceneColorCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGSceneColorCopyPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGSceneColorCopyPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.SceneColorCopy"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
