#include "FRDGUIChromaticAberrationPass.h"

#include "FPostProcessStructures.h"
#include "RDGChromaticAberrationPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

FVector2f FRDGUIChromaticAberrationPass::BuildChromaticScale(float Intensity, float StartOffset)
{
	const float Offset = Intensity * 0.01f;
	const float Multiplier =
		(StartOffset < 1.0f - KINDA_SMALL_NUMBER)
		? 1.0f / (1.0f - StartOffset)
		: 1.0f;

	// Wavelengths of RGB primaries in nm from UE tonemap implementation.
	const float PrimaryR = 611.3f;
	const float PrimaryG = 549.1f;
	const float PrimaryB = 464.3f;

	// Simple lens chromatic aberration is approximated as linear in wavelength.
	const float ScaleR = 0.007f * (PrimaryR - PrimaryB);
	const float ScaleG = 0.007f * (PrimaryG - PrimaryB);

	return FVector2f(Offset * ScaleR * Multiplier, Offset * ScaleG * Multiplier);
}

FScreenPassTexture FRDGUIChromaticAberrationPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FUIChromaticAberrationParameters& Parameters)
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
		TEXT("RDG.UIChromatic.Output"));

	FScreenPassRenderTarget Output(
		OutputTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FRGDChromaticAberrationPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRGDChromaticAberrationPS::FParameters>();

	PassParameters->InputTexture = Input.Texture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->bEnabled = Parameters.bEnabled ? 1u : 0u;
	PassParameters->StartOffset = Parameters.StartOffset;
	PassParameters->CAScale = BuildChromaticScale(Parameters.Intensity, Parameters.StartOffset);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRGDChromaticAberrationPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.UIChromatic"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
