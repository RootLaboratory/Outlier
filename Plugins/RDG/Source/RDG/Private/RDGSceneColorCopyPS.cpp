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

namespace
{
	FScreenPassRenderTarget ResolveCopyOutput(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& SceneColor,
		const FScreenPassRenderTarget& OverrideOutput)
	{
		if (OverrideOutput.IsValid())
		{
			return OverrideOutput;
		}

		return FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			ERenderTargetLoadAction::ENoAction,
			TEXT("RDG.SceneColorCopy.Output"));
	}

	FRDGSceneColorCopyPS::FParameters* BuildCopyParameters(
		FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& SceneColor,
		const FScreenPassRenderTarget& Output)
	{
		FRDGSceneColorCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGSceneColorCopyPS::FParameters>();
		PassParameters->InputTexture = SceneColor.Texture;
		PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
		return PassParameters;
	}
}

FScreenPassTexture FRDGSceneColorCopyPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	FScreenPassRenderTarget Output = ResolveCopyOutput(GraphBuilder, SceneColor, OverrideOutput);
	FRDGSceneColorCopyPS::FParameters* PassParameters = BuildCopyParameters(GraphBuilder, SceneColor, Output);

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

FScreenPassTexture FRDGSceneColorCopyPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& SceneColor,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	FScreenPassRenderTarget Output = ResolveCopyOutput(GraphBuilder, SceneColor, OverrideOutput);
	FRDGSceneColorCopyPS::FParameters* PassParameters = BuildCopyParameters(GraphBuilder, SceneColor, Output);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGSceneColorCopyPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.SceneColorCopy"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
