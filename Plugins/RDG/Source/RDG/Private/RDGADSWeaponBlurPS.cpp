#include "RDGADSWeaponBlurPS.h"

#include "FPostProcessStructures.h"
#include "FRDGADSWeaponBlurPass.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

bool FRDGADSWeaponBlurPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGADSWeaponBlurPS, "/Plugin/RDG/ADSWeaponBlur.usf", "MainPS", SF_Pixel);

static FVector4f GetViewportUVToTextureUVScaleBias(const FIntPoint& TextureExtent, const FIntRect& TextureViewRect)
{
	const FVector2f InvExtent(
		1.0f / FMath::Max(1, TextureExtent.X),
		1.0f / FMath::Max(1, TextureExtent.Y));

	return FVector4f(
		static_cast<float>(TextureViewRect.Width()) * InvExtent.X,
		static_cast<float>(TextureViewRect.Height()) * InvExtent.Y,
		static_cast<float>(TextureViewRect.Min.X) * InvExtent.X,
		static_cast<float>(TextureViewRect.Min.Y) * InvExtent.Y);
}

static FScreenPassTexture RenderADSBlurStage(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& Input,
	FRDGTextureRef OutputTexture,
	const FIntRect& OutputRect,
	ERenderTargetLoadAction OutputLoadAction,
	FRDGTextureRef MaskTexture,
	FRDGTextureRef HardMaskTexture,
	FRDGTextureRef CustomDepthTexture,
	float KernelOffset,
	bool bUpsample,
	bool bCompositeWithOriginal,
	const FScreenPassTexture& OriginalTexture,
	float BlendWeight,
	float InnerPreserve,
	float DepthBlurStart,
	float DepthBlurEnd,
	float DepthBlurPower,
	float DepthFocusBias,
	float FocusDistanceWorld,
	float MaxCoCRadius,
	uint32 GatherSampleCount,
	float ReachSoftness,
	bool bGather,
	const TCHAR* EventName)
{
	FScreenPassRenderTarget Output(OutputTexture, OutputRect, OutputLoadAction);

	FRDGADSWeaponBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGADSWeaponBlurPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->OriginalTexture = OriginalTexture.IsValid() ? OriginalTexture.Texture : Input.Texture;
	PassParameters->MaskTexture = MaskTexture;
	PassParameters->HardMaskTexture = HardMaskTexture ? HardMaskTexture : MaskTexture;
	PassParameters->CustomDepthTexture = CustomDepthTexture ? CustomDepthTexture : FRDGSystemTextures::Get(GraphBuilder).Black;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FScreenPassTextureViewport InputViewport(Input);
	PassParameters->InvResolution = FVector2f(1.0f / FMath::Max(1, InputViewport.Extent.X), 1.0f / FMath::Max(1, InputViewport.Extent.Y));
	PassParameters->MaskTextureInvResolution = MaskTexture
		? FVector2f(
			1.0f / FMath::Max(1, MaskTexture->Desc.Extent.X),
			1.0f / FMath::Max(1, MaskTexture->Desc.Extent.Y))
		: FVector2f(1.0f, 1.0f);
	PassParameters->CustomDepthTextureInvResolution = CustomDepthTexture
		? FVector2f(
			1.0f / FMath::Max(1, CustomDepthTexture->Desc.Extent.X),
			1.0f / FMath::Max(1, CustomDepthTexture->Desc.Extent.Y))
		: FVector2f(1.0f, 1.0f);

	const FScreenPassTextureViewport OriginalViewport(OriginalTexture.IsValid() ? OriginalTexture : Input);
	const FIntRect CustomDepthViewRect = CustomDepthTexture
		? FIntRect(View.UnscaledViewRect.Min, View.UnscaledViewRect.Min + View.UnscaledViewRect.Size())
		: OriginalViewport.Rect;
	const FIntPoint CustomDepthExtent = CustomDepthTexture ? CustomDepthTexture->Desc.Extent : OriginalViewport.Extent;
	const FIntRect MaskViewRect = MaskTexture ? FIntRect(FIntPoint::ZeroValue, MaskTexture->Desc.Extent) : OriginalViewport.Rect;
	const FIntPoint MaskExtent = MaskTexture ? MaskTexture->Desc.Extent : OriginalViewport.Extent;
	PassParameters->ViewportUVToOriginalTextureUV = GetViewportUVToTextureUVScaleBias(OriginalViewport.Extent, OriginalViewport.Rect);
	PassParameters->ViewportUVToCustomDepthTextureUV = GetViewportUVToTextureUVScaleBias(CustomDepthExtent, CustomDepthViewRect);
	PassParameters->ViewportUVToMaskTextureUV = GetViewportUVToTextureUVScaleBias(MaskExtent, MaskViewRect);
	PassParameters->KernelOffset = KernelOffset;
	PassParameters->BlendWeight = BlendWeight;
	PassParameters->InnerPreserve = InnerPreserve;
	PassParameters->DepthBlurStart = DepthBlurStart;
	PassParameters->DepthBlurEnd = DepthBlurEnd;
	PassParameters->DepthBlurPower = DepthBlurPower;
	PassParameters->DepthFocusBias = DepthFocusBias;
	PassParameters->FocusDistanceWorld = FocusDistanceWorld;
	PassParameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);
	PassParameters->MaxCoCRadius = MaxCoCRadius;
	PassParameters->GatherSampleCount = FMath::Clamp(GatherSampleCount, 1u, 96u);
	PassParameters->ReachSoftness = FMath::Max(0.0f, ReachSoftness);
	PassParameters->bGather = bGather ? 1u : 0u;
	PassParameters->bUpsample = bUpsample ? 1u : 0u;
	PassParameters->bCompositeWithOriginal = bCompositeWithOriginal ? 1u : 0u;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGADSWeaponBlurPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", EventName),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

FScreenPassTexture FRDGADSWeaponBlurPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	FRDGTextureRef MaskTexture,
	FRDGTextureRef HardMaskTexture,
	FRDGTextureRef CustomDepthTexture,
	const FADSBlurParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0 || !MaskTexture || Parameters.AdsBlend <= KINDA_SMALL_NUMBER)
	{
		return SceneColor;
	}

	// BlurRadius를 최대 CoC 반경(px)으로 재해석. AdsBlend ramp에 따라 0 -> max로 증가.
	const float MaxCoCRadius = FMath::Max(0.0f, Parameters.BlurRadius);

	FScreenPassRenderTarget FinalOutput = OverrideOutput;
	if (!FinalOutput.IsValid())
	{
		FRDGTextureDesc FinalDesc = SceneColor.Texture->Desc;
		EnumRemoveFlags(FinalDesc.Flags, ETextureCreateFlags::Presentable);
		FinalDesc.Reset();
		FinalDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

		FRDGTextureRef FinalTexture = GraphBuilder.CreateTexture(
			FinalDesc,
			TEXT("RDG.ADSWeaponBlur.Final"));

		FinalOutput = FScreenPassRenderTarget(
			FinalTexture,
			SceneColor.ViewRect,
			ERenderTargetLoadAction::ENoAction);
	}

	// 단일 풀해상도 Scatter-as-Gather 패스: 무기 픽셀만 CoC 기반 가변 반경으로 gather + 합성.
	return RenderADSBlurStage(
		GraphBuilder,
		View,
		SceneColor,
		FinalOutput.Texture,
		FinalOutput.ViewRect,
		FinalOutput.LoadAction,
		MaskTexture,
		HardMaskTexture,
		CustomDepthTexture,
		0.0f,        // KernelOffset (gather에서 미사용)
		false,       // bUpsample
		false,       // bCompositeWithOriginal (gather 브랜치가 자체 합성)
		SceneColor,  // OriginalTexture (gather 소스)
		Parameters.AdsBlend,
		Parameters.InnerPreserve,
		Parameters.DepthBlurStart,
		Parameters.DepthBlurEnd,
		Parameters.DepthBlurPower,
		Parameters.DepthFocusBias,
		Parameters.FocusDistanceWorld,
		MaxCoCRadius,
		static_cast<uint32>(Parameters.GatherSampleCount),
		Parameters.ReachSoftness,
		true,        // bGather
		TEXT("RDG.ADSWeaponBlur.Gather"));
}
