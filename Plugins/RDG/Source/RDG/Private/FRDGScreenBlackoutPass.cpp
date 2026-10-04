#include "FRDGScreenBlackoutPass.h"

#include "FPostProcessStructures.h"
#include "RDGScreenBlackoutPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGScreenBlackoutPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FScreenBlackoutParameters& Parameters)
{
	const float Alpha = FMath::Clamp(Parameters.Alpha, 0.0f, 1.0f);
	if (!Input.IsValid() || !Parameters.bEnabled || Alpha <= 0.0f)
	{
		return Input;
	}

	FRDGTextureDesc OutputDesc = Input.Texture->Desc;
	EnumRemoveFlags(OutputDesc.Flags, ETextureCreateFlags::Presentable);
	OutputDesc.Reset();
	OutputDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(
		OutputDesc,
		TEXT("RDG.ScreenBlackout.Output"));

	FScreenPassRenderTarget Output(
		OutputTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FRDGScreenBlackoutPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGScreenBlackoutPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->Alpha = Alpha;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGScreenBlackoutPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.ScreenBlackout"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
