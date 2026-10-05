#include "FRDGDeathColorLerpPass.h"

#include "RDGDeathColorLerpPS.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "UnrealClient.h"

FScreenPassTexture FRDGDeathColorLerpPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FLinearColor& LerpColor,
	float Amount,
	FRHITexture* VignetteTexture,
	float VignetteAmount,
	const TCHAR* PassName,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 보간량이 0이어도 그 경우엔 그린다.
	const float ClampedAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	const float ClampedVignetteAmount = VignetteTexture ? FMath::Clamp(VignetteAmount, 0.0f, 1.0f) : 0.0f;
	if (ClampedAmount <= 0.0f && ClampedVignetteAmount <= 0.0f && !OverrideOutput.IsValid())
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
			TEXT("RDG.DeathColorLerp.Output"));
	}

	// Tonemap 이후 장면색은 이미 sRGB 인코딩된 값이라, sRGB 텍스처는 샘플링 결과(선형)를 다시 인코딩해서 맞춘다.
	static const TConsoleVariableData<float>* TonemapperGamma = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.TonemapperGamma"));
	const bool bDefaultTonemapperGamma = !TonemapperGamma || TonemapperGamma->GetValueOnRenderThread() <= 0.0f;
	const bool bSDRSRGBOutput = View.Family && View.Family->RenderTarget
		&& View.Family->RenderTarget->GetDisplayOutputFormat() == EDisplayOutputFormat::SDR_sRGB;
	const bool bSRGBRenderTarget = Output.Texture && EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB);
	const bool bSRGBVignetteTexture = VignetteTexture && EnumHasAnyFlags(VignetteTexture->GetDesc().Flags, TexCreate_SRGB);

	FRDGDeathColorLerpPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDeathColorLerpPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->VignetteTexture = VignetteTexture ? VignetteTexture : GBlackTexture->TextureRHI.GetReference();
	PassParameters->VignetteSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->LerpColor = FVector3f(LerpColor.R, LerpColor.G, LerpColor.B);
	PassParameters->Amount = ClampedAmount;
	PassParameters->VignetteAmount = ClampedVignetteAmount;
	PassParameters->bEncodeVignetteToSRGB = bSDRSRGBOutput && bDefaultTonemapperGamma
		&& bSRGBVignetteTexture && !bSRGBRenderTarget ? 1 : 0;
	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FVector2f InputExtent(InputViewport.Extent.X, InputViewport.Extent.Y);
	const FVector2f InputMin(InputViewport.Rect.Min.X, InputViewport.Rect.Min.Y);
	const FVector2f InputSize(InputViewport.Rect.Width(), InputViewport.Rect.Height());
	PassParameters->InputUVMin = InputMin / InputExtent;
	PassParameters->InputUVSizeInverse = InputExtent / InputSize;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDeathColorLerpPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", PassName),
		View,
		FScreenPassTextureViewport(Output),
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
