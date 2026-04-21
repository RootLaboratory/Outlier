#include "RDGMotionBlurPS.h"

#include "FPostProcessStructures.h"
#include "FRDGMotionBlurPass.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

bool FRDGMotionBlurPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGMotionBlurPS, "/Plugin/RDG/MotionBlur.usf", "MainPS", SF_Pixel);

FScreenPassTexture FRDGMotionBlurPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FMotionBlurParameters& Parameters)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0)
	{
		//UE_LOG(LogTemp, Error, TEXT("ADDPASS RETURN SceneColor"));
		return SceneColor;
	}

	FScreenPassRenderTarget Output = FScreenPassRenderTarget::CreateFromInput(
		GraphBuilder,
		SceneColor,
		ERenderTargetLoadAction::ENoAction,
		TEXT("RDG.MotionBlur.Output"));

	FRDGMotionBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGMotionBlurPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->InputSampler =
	TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FScreenPassTextureViewport InputViewport(SceneColor);// Viewport size
	const FVector2D RectToExtent = InputViewport.GetRectToExtentRatio();
	PassParameters->ViewRectMinUV = FVector2f(0.0f, 0.0f);
	PassParameters->ViewRectMaxUV = FVector2f((float)RectToExtent.X, (float)RectToExtent.Y);

	PassParameters->BlendWeight = Parameters.BlendWeight;
	PassParameters->Intensity = Parameters.Intensity;
	PassParameters->VelocityScale = Parameters.VelocityScale;
	PassParameters->bEnabled = Parameters.bEnabled ? 1u : 0u;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGMotionBlurPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.MotionBlur"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
