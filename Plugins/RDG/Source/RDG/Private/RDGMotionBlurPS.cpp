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
	const FMotionBlurParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0)
	{
		return SceneColor;
	}

	FScreenPassRenderTarget Output = OverrideOutput;
	if (Output.IsValid())
	{
		const bool bPartialOutput =
			Output.ViewRect.Min != FIntPoint::ZeroValue ||
			(Output.Texture && Output.Texture->Desc.Extent != Output.ViewRect.Max);
		if (bPartialOutput)
		{
			Output.LoadAction = ERenderTargetLoadAction::ELoad;
		}
	}
	else
	{
		Output = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			ERenderTargetLoadAction::ENoAction,
			TEXT("RDG.MotionBlur.Output"));
	}

	FRDGMotionBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGMotionBlurPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FScreenPassTextureViewport InputViewport(SceneColor);
	const FVector2f InvExtent(
		1.0f / FMath::Max(1, InputViewport.Extent.X),
		1.0f / FMath::Max(1, InputViewport.Extent.Y));
	const FVector2f BilinearMinUV(
		(static_cast<float>(InputViewport.Rect.Min.X) + 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Min.Y) + 0.5f) * InvExtent.Y);
	const FVector2f BilinearMaxUV(
		(static_cast<float>(InputViewport.Rect.Max.X) - 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Max.Y) - 0.5f) * InvExtent.Y);

	PassParameters->ViewRectMinUV = BilinearMinUV;
	PassParameters->ViewRectMaxUV = FVector2f(
		FMath::Max(BilinearMinUV.X, BilinearMaxUV.X),
		FMath::Max(BilinearMinUV.Y, BilinearMaxUV.Y));

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
