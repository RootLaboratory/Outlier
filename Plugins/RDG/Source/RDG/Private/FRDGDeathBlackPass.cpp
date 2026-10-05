#include "FRDGDeathBlackPass.h"

#include "FPostProcessStructures.h"
#include "RDGDeathBlackPS.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "UnrealClient.h"

FScreenPassTexture FRDGDeathBlackPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FDeathBlackParameters& Parameters,
	FRHITexture* BackgroundTexture,
	FRHITexture* NoiseTexture,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || !BackgroundTexture || !NoiseTexture)
	{
		return SceneColor;
	}

	// 레이어마다 등장 시점부터 페이드 시간 동안 0 → 1.
	const bool bEnabled = Parameters.bEnabled != 0;
	const float BackgroundAmount = bEnabled
		? GetDeathBlackLayerAmount(Parameters, EDeathBlackLayer::Background) : 0.0f;
	const float NoiseAmount = bEnabled
		? GetDeathBlackLayerAmount(Parameters, EDeathBlackLayer::Noise) : 0.0f;

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 아무 레이어도 안 나왔어도 그 경우엔 그린다.
	const bool bAnyLayerVisible = BackgroundAmount > 0.0f || NoiseAmount > 0.0f;
	if (!bAnyLayerVisible && !OverrideOutput.IsValid())
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
			TEXT("RDG.DeathBlack.Output"));
	}

	// Tonemap 이후 장면색은 이미 sRGB 인코딩된 값이라, sRGB 텍스처는 샘플링 결과(선형)를 다시 인코딩해서 맞춘다.
	static const TConsoleVariableData<float>* TonemapperGamma = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.TonemapperGamma"));
	const bool bDefaultTonemapperGamma = !TonemapperGamma || TonemapperGamma->GetValueOnRenderThread() <= 0.0f;
	const bool bSDRSRGBOutput = View.Family && View.Family->RenderTarget
		&& View.Family->RenderTarget->GetDisplayOutputFormat() == EDisplayOutputFormat::SDR_sRGB;
	const bool bSRGBRenderTarget = Output.Texture && EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB);
	const bool bEncodeSRGBTextures = bSDRSRGBOutput && bDefaultTonemapperGamma && !bSRGBRenderTarget;
	auto ShouldEncodeToSRGB = [bEncodeSRGBTextures](const FRHITexture* Texture) -> int32
	{
		return bEncodeSRGBTextures && Texture && EnumHasAnyFlags(Texture->GetDesc().Flags, TexCreate_SRGB) ? 1 : 0;
	};

	FRDGDeathBlackPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDeathBlackPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->BackgroundTexture = BackgroundTexture;
	PassParameters->NoiseTexture = NoiseTexture;
	PassParameters->LayerSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->BackgroundAmount = BackgroundAmount;
	PassParameters->NoiseAmount = NoiseAmount;
	PassParameters->bEncodeBackgroundToSRGB = ShouldEncodeToSRGB(BackgroundTexture);
	PassParameters->bEncodeNoiseToSRGB = ShouldEncodeToSRGB(NoiseTexture);
	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FVector2f InputExtent(InputViewport.Extent.X, InputViewport.Extent.Y);
	const FVector2f InputMin(InputViewport.Rect.Min.X, InputViewport.Rect.Min.Y);
	const FVector2f InputSize(InputViewport.Rect.Width(), InputViewport.Rect.Height());
	PassParameters->InputUVMin = InputMin / InputExtent;
	PassParameters->InputUVSizeInverse = InputExtent / InputSize;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDeathBlackPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.DeathBlack"),
		View,
		FScreenPassTextureViewport(Output),
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
