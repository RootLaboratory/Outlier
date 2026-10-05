#include "FRDGDroneDamageFeedbackPass.h"

#include "FRDGPixelSortingPass.h"
#include "GlobalRenderResources.h"
#include "RDGDroneDamageFeedbackPS.h"
#include "RenderGraphBuilder.h"
#include "RenderResource.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"

FScreenPassTexture FRDGDroneDamageFeedbackPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FDroneDamageFeedbackParameters& Parameters,
	const TStaticArray<FRHITexture*, DroneDamageMaskCount>& MaskTextures,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 꺼져 있어도 그 경우엔
	// 마스크 0(원본 그대로 통과)으로 그린다.
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
			TEXT("RDG.DroneDamageFeedback.Output"));
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

	// 비어 있는 슬롯은 검은 텍스처로 채우고 가중치를 0으로 둔다.
	FRHITexture* BoundMasks[DroneDamageMaskCount];
	float Weights[DroneDamageMaskCount];
	float SRGBFlags[DroneDamageMaskCount];
	for (int32 Index = 0; Index < DroneDamageMaskCount; ++Index)
	{
		FRHITexture* Mask = MaskTextures[Index];
		BoundMasks[Index] = Mask ? Mask : GBlackTexture->TextureRHI.GetReference();
		Weights[Index] = bEnabled && Mask ? FMath::Clamp(Parameters.MaskWeights[Index], 0.0f, 1.0f) : 0.0f;
		SRGBFlags[Index] = Mask && EnumHasAnyFlags(Mask->GetDesc().Flags, TexCreate_SRGB) ? 1.0f : 0.0f;
	}

	// 픽셀 소팅은 기존 Pixel Sorting 패스로 화면 전체를 정렬해 두고, 셰이더가 마스크 안에서만 읽는다.
	// 정렬할 게 없으면 입력이 그대로 돌아온다.
	const bool bPixelSort = bEnabled && Parameters.bPixelSort != 0;
	FScreenPassTexture Sorted = SceneColor;
	if (bPixelSort)
	{
		FPixelSortingParameters SortParameters;
		SortParameters.bEnabled = 1;
		SortParameters.Mode = Parameters.PixelSortMode;
		SortParameters.Threshold = FMath::Clamp(Parameters.PixelSortThreshold, 0.0f, 255.0f);
		SortParameters.Progress = 1.0f;
		SortParameters.bColorInterpolationEnabled = 0;
		SortParameters.bSortRows = Parameters.bPixelSortRows;
		SortParameters.bSortColumns = Parameters.bPixelSortColumns;
		SortParameters.ResolutionDivisor = Parameters.PixelSortResolutionDivisor;
		Sorted = FRDGPixelSortingPass::AddPass(GraphBuilder, View, SceneColor, SortParameters);
	}

	const FScreenPassTextureViewport SortedViewport(Sorted);
	const FVector2f SortedInvExtent(
		1.0f / FMath::Max(1, SortedViewport.Extent.X),
		1.0f / FMath::Max(1, SortedViewport.Extent.Y));
	const FIntPoint SortedSize = SortedViewport.Rect.Size();

	FRDGDroneDamageFeedbackPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGDroneDamageFeedbackPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->SortedTexture = Sorted.Texture;
	PassParameters->SortedUVOrigin = FVector2f(
		static_cast<float>(SortedViewport.Rect.Min.X) * SortedInvExtent.X,
		static_cast<float>(SortedViewport.Rect.Min.Y) * SortedInvExtent.Y);
	PassParameters->SortedUVSize = FVector2f(
		static_cast<float>(FMath::Max(1, SortedSize.X)) * SortedInvExtent.X,
		static_cast<float>(FMath::Max(1, SortedSize.Y)) * SortedInvExtent.Y);
	PassParameters->SortedUVMin = FVector2f(
		(static_cast<float>(SortedViewport.Rect.Min.X) + 0.5f) * SortedInvExtent.X,
		(static_cast<float>(SortedViewport.Rect.Min.Y) + 0.5f) * SortedInvExtent.Y);
	PassParameters->SortedUVMax = FVector2f(
		FMath::Max(PassParameters->SortedUVMin.X, (static_cast<float>(SortedViewport.Rect.Max.X) - 0.5f) * SortedInvExtent.X),
		FMath::Max(PassParameters->SortedUVMin.Y, (static_cast<float>(SortedViewport.Rect.Max.Y) - 0.5f) * SortedInvExtent.Y));
	PassParameters->MaskTexture0 = BoundMasks[0];
	PassParameters->MaskTexture1 = BoundMasks[1];
	PassParameters->MaskTexture2 = BoundMasks[2];
	PassParameters->MaskTexture3 = BoundMasks[3];
	PassParameters->MaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MaskWeights = FVector4f(Weights[0], Weights[1], Weights[2], Weights[3]);
	PassParameters->MaskSRGB = FVector4f(SRGBFlags[0], SRGBFlags[1], SRGBFlags[2], SRGBFlags[3]);
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
	PassParameters->bSliceGlitch = Parameters.bSliceGlitch ? 1u : 0u;
	PassParameters->bPixelSort = bPixelSort ? 1u : 0u;
	PassParameters->bShiftToBlack = Parameters.bShiftToBlack ? 1u : 0u;
	PassParameters->Time = FMath::Max(0.0f, Parameters.Time);
	PassParameters->Intensity = FMath::Max(0.0f, Parameters.Intensity);
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
	PassParameters->ShiftOffset = FVector2f(Parameters.ShiftOffsetX, Parameters.ShiftOffsetY);
	PassParameters->ShiftJitter = FMath::Max(0.0f, Parameters.ShiftJitter);
	PassParameters->MaskGlow = FMath::Max(0.0f, Parameters.MaskGlow);
	PassParameters->Tint = FVector3f(Parameters.Tint.R, Parameters.Tint.G, Parameters.Tint.B);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDroneDamageFeedbackPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.DroneDamageFeedback"),
		View,
		FScreenPassTextureViewport(Output),
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
