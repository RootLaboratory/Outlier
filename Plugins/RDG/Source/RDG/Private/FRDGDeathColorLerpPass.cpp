#include "FRDGDeathColorLerpPass.h"

#include "RDGDeathColorLerpPS.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGDeathColorLerpPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FLinearColor& LerpColor,
	float Amount,
	const TCHAR* PassName,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 보간량이 0이어도 그 경우엔 그린다.
	const float ClampedAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	if (ClampedAmount <= 0.0f && !OverrideOutput.IsValid())
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

	FRDGDeathColorLerpPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDeathColorLerpPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->LerpColor = FVector3f(LerpColor.R, LerpColor.G, LerpColor.B);
	PassParameters->Amount = ClampedAmount;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDeathColorLerpPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", PassName),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
