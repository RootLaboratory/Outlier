#include "FRDGDeathNoisePass.h"

#include "FPostProcessStructures.h"
#include "RDGDeathNoisePS.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGDeathNoisePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FDeathNoiseParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 꺼져 있어도 그 경우엔
	// 세기 0(원본 그대로 통과)으로 그린다.
	const bool bEnabled = Parameters.bEnabled != 0;
	if (!bEnabled && !OverrideOutput.IsValid())
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
			TEXT("RDG.DeathNoise.Output"));
	}

	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FVector2f InvExtent(
		1.0f / FMath::Max(1, InputViewport.Extent.X),
		1.0f / FMath::Max(1, InputViewport.Extent.Y));
	const FVector2f BilinearMinUV(
		(static_cast<float>(InputViewport.Rect.Min.X) + 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Min.Y) + 0.5f) * InvExtent.Y);
	const FVector2f BilinearMaxUV(
		(static_cast<float>(InputViewport.Rect.Max.X) - 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Max.Y) - 0.5f) * InvExtent.Y);
	const FIntPoint ViewSize = InputViewport.Rect.Size();

	FRDGDeathNoisePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDeathNoisePS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ViewRectMinUV = BilinearMinUV;
	PassParameters->ViewRectMaxUV = FVector2f(
		FMath::Max(BilinearMinUV.X, BilinearMaxUV.X),
		FMath::Max(BilinearMinUV.Y, BilinearMaxUV.Y));
	PassParameters->ViewportUVOrigin = FVector2f(
		static_cast<float>(InputViewport.Rect.Min.X) * InvExtent.X,
		static_cast<float>(InputViewport.Rect.Min.Y) * InvExtent.Y);
	PassParameters->ViewportUVSize = FVector2f(
		static_cast<float>(FMath::Max(1, ViewSize.X)) * InvExtent.X,
		static_cast<float>(FMath::Max(1, ViewSize.Y)) * InvExtent.Y);

	PassParameters->Time = FMath::Max(0.0f, Parameters.Time);
	PassParameters->Intensity = bEnabled ? FMath::Max(0.0f, Parameters.Intensity) : 0.0f;
	PassParameters->Seed = Parameters.Seed;
	PassParameters->SliceRows = FMath::Max(1.0f, Parameters.SliceRows);
	PassParameters->SliceSplitChance = FMath::Clamp(Parameters.SliceSplitChance, 0.0f, 1.0f);
	PassParameters->GlitchRate = FMath::Max(1.0f, Parameters.GlitchRate);
	PassParameters->GlitchStrength = FMath::Max(0.0f, Parameters.GlitchStrength);
	PassParameters->GlitchThreshold = FMath::Clamp(Parameters.GlitchThreshold, 0.0f, 1.0f);
	PassParameters->GlitchGlow = FMath::Max(0.0f, Parameters.GlitchGlow);
	PassParameters->BurstChance = FMath::Clamp(Parameters.BurstChance, 0.0f, 1.0f);
	PassParameters->BurstStrength = FMath::Max(1.0f, Parameters.BurstStrength);
	PassParameters->BurstThreshold = FMath::Clamp(Parameters.BurstThreshold, 0.0f, 1.0f);
	PassParameters->Tint = FVector3f(Parameters.Tint.R, Parameters.Tint.G, Parameters.Tint.B);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDeathNoisePS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.DeathNoise"),
		View,
		FScreenPassTextureViewport(Output),
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
