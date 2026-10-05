#include "FRDGSplitPrismPass.h"

#include "FPostProcessStructures.h"
#include "RDGSplitPrismPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGSplitPrismPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FSplitPrismParameters& Parameters)
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
		TEXT("RDG.SplitPrism.Output"));

	FScreenPassRenderTarget Output(
		OutputTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FRDGSplitPrismPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGSplitPrismPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->Offset = Parameters.Offset;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGSplitPrismPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.SplitPrism"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
