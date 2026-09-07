#include "FRDGOverlayPass.h"

#include "FPostProcessStructures.h"
#include "RDGOverlayPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGOverlayPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FOverlayParameters& Parameters)
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
		TEXT("RDG.Overlay.Output"));

	FScreenPassRenderTarget Output(
		OutputTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FRDGOverlayPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGOverlayPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->TintColor = FVector3f(
		Parameters.TintColor.R,
		Parameters.TintColor.G,
		Parameters.TintColor.B);
	PassParameters->Intensity = FMath::Clamp(Parameters.AccumulatedValue, 0.0f, 1.0f);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGOverlayPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.Overlay"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
