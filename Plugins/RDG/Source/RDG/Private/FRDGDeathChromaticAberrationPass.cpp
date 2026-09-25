#include "FRDGDeathChromaticAberrationPass.h"

#include "FPostProcessStructures.h"
#include "RDGDeathChromaticAberrationPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGDeathChromaticAberrationPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FDeathChromaticAberrationParameters& Parameters)
{
	if (!Input.IsValid() || !Parameters.bEnabled)
	{
		return Input;
	}

	FRDGTextureDesc OutputDesc = Input.Texture->Desc;
	EnumRemoveFlags(OutputDesc.Flags, ETextureCreateFlags::Presentable);
	OutputDesc.Reset();
	OutputDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(
		OutputDesc,
		TEXT("RDG.DeathChromaticAberration.Output"));

	FScreenPassRenderTarget Output(
		OutputTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FRDGDeathChromaticAberrationPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGDeathChromaticAberrationPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->Offset = FVector2f(Parameters.OffsetX, Parameters.OffsetY);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDeathChromaticAberrationPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.DeathChromaticAberration"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
