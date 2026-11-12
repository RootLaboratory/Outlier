#include "FRDGPartnerKnockDownPass.h"

#include "FPostProcessStructures.h"
#include "FRDGUIChromaticAberrationPass.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "RDGPartnerKnockDownPS.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "UnrealClient.h"

float FRDGPartnerKnockDownPass::GetProgress(const FPartnerKnockDownParameters& Parameters)
{
	if (Parameters.bProgressOverride)
	{
		return FMath::Clamp(Parameters.ProgressOverride, 0.0f, 1.0f);
	}

	return Parameters.Duration > 0.0f
		? FMath::Clamp(Parameters.Time / Parameters.Duration, 0.0f, 1.0f)
		: 0.0f;
}

float FRDGPartnerKnockDownPass::GetDamageHudOpacity(const FPartnerKnockDownParameters& Parameters)
{
	const FPartnerKnockDownDamageHudParameters& DamageHud = Parameters.DamageHud;
	const float Opacity = FMath::Clamp(DamageHud.Opacity, 0.0f, 1.0f);
	const float Remaining = (1.0f - GetProgress(Parameters)) * FMath::Max(0.0f, Parameters.Duration);
	if (DamageHud.FadeOutTime <= 0.0f)
	{
		return Remaining > 0.0f ? Opacity : 0.0f;
	}

	return Opacity * FMath::Clamp(Remaining / DamageHud.FadeOutTime, 0.0f, 1.0f);
}

float FRDGPartnerKnockDownPass::EvaluateDamageHudPulse(const FPartnerKnockDownParameters& Parameters, float MinValue, float MaxValue)
{
	const FPartnerKnockDownDamageHudParameters& DamageHud = Parameters.DamageHud;
	const float InTime = FMath::Max(0.0f, DamageHud.PulseInTime);
	const float OutTime = FMath::Max(0.0f, DamageHud.PulseOutTime);
	const float Time = FMath::Max(0.0f, Parameters.Time);

	// Min → Max: 천천히 출발해서 빨라지는 EaseIn.
	if (Time < InTime)
	{
		return FMath::InterpEaseIn(MinValue, MaxValue, Time / InTime, 2.0f);
	}

	// Max → Min: 빠르게 출발해서 천천히 멈추는 EaseOut. 끝나면 Min에 머문다.
	const float OutElapsed = Time - InTime;
	if (OutTime > 0.0f && OutElapsed < OutTime)
	{
		return FMath::InterpEaseOut(MaxValue, MinValue, OutElapsed / OutTime, 2.0f);
	}

	return MinValue;
}

float FRDGPartnerKnockDownPass::GetDamageHudExponentPulse(const FPartnerKnockDownParameters& Parameters)
{
	return EvaluateDamageHudPulse(
		Parameters,
		FMath::Max(0.0f, Parameters.DamageHud.MinExponent),
		FMath::Max(0.0f, Parameters.DamageHud.MaxExponent));
}

float FRDGPartnerKnockDownPass::GetDamageHudIntensity(const FPartnerKnockDownParameters& Parameters)
{
	return EvaluateDamageHudPulse(
		Parameters,
		FMath::Max(0.0f, Parameters.DamageHud.MinIntensity),
		FMath::Max(0.0f, Parameters.DamageHud.MaxIntensity));
}

float FRDGPartnerKnockDownPass::GetDamageHudExponent(const FPartnerKnockDownParameters& Parameters)
{
	// 펄스값이 클수록 지수가 1보다 작아져 텍스처 값이 올라간다. 펄스 0 = 지수 1 = 원본.
	return 1.0f / (1.0f + GetDamageHudExponentPulse(Parameters));
}

float FRDGPartnerKnockDownPass::GetDamageHudBreath(const FPartnerKnockDownParameters& Parameters)
{
	const FPartnerKnockDownDamageHudParameters& DamageHud = Parameters.DamageHud;
	if (!DamageHud.bBreath)
	{
		return 1.0f;
	}

	// 펄스(Max → 기본)가 끝난 순간부터 센다.
	const float PulseEnd = FMath::Max(0.0f, DamageHud.PulseInTime) + FMath::Max(0.0f, DamageHud.PulseOutTime);
	const float BreathElapsed = FMath::Max(0.0f, Parameters.Time) - PulseEnd;
	if (BreathElapsed <= 0.0f)
	{
		return 1.0f;
	}

	// cos로 시작해서 첫 값이 1이라 펄스 끝과 끊기지 않는다.
	const float Period = FMath::Max(0.05f, DamageHud.BreathPeriod);
	const float Phase = 0.5f + 0.5f * FMath::Cos(UE_TWO_PI * BreathElapsed / Period);
	return FMath::Lerp(FMath::Clamp(DamageHud.BreathMin, 0.0f, 1.0f), 1.0f, Phase);
}

FRDGTextureRef FRDGPartnerKnockDownPass::AddMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FIntPoint& ViewSize,
	const FPartnerKnockDownParameters& Parameters,
	FRHITexture* MaskTexture)
{
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		FIntPoint(FMath::Max(1, ViewSize.X), FMath::Max(1, ViewSize.Y)),
		PF_R8,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef WeightTexture = GraphBuilder.CreateTexture(Desc, TEXT("RDG.PartnerKnockDown.Mask"));

	FRDGPartnerKnockDownMaskPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGPartnerKnockDownMaskPS::FParameters>();
	// 마스크가 없으면 전 화면 1. 끝날 때 한꺼번에 사라진다.
	PassParameters->MaskTexture = MaskTexture ? MaskTexture : GWhiteTexture->TextureRHI.GetReference();
	PassParameters->MaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MaskBlackPoint = FMath::Clamp(Parameters.MaskBlackPoint, 0.0f, 1.0f);
	PassParameters->MaskWhitePoint = FMath::Clamp(Parameters.MaskWhitePoint, 0.0f, 1.0f);
	PassParameters->MaskGamma = FMath::Clamp(Parameters.MaskGamma, 0.05f, 8.0f);
	PassParameters->Progress = GetProgress(Parameters);
	PassParameters->RecoverSoftness = FMath::Clamp(Parameters.RecoverSoftness, 0.0f, 0.5f);
	PassParameters->bMaskInvert = MaskTexture && Parameters.bMaskInvert ? 1 : 0;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(WeightTexture, ERenderTargetLoadAction::ENoAction);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGPartnerKnockDownMaskPS> PixelShader(ShaderMap);

	// 출력 전체가 뷰 하나라서 입력 / 출력 뷰포트를 같은 걸로 넘겨 Input.UV를 0~1로 받는다.
	const FScreenPassTextureViewport Viewport(WeightTexture);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.PartnerKnockDown.Mask"),
		View,
		Viewport,
		Viewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return WeightTexture;
}

FScreenPassTexture FRDGPartnerKnockDownPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FPartnerKnockDownParameters& Parameters,
	FRHITexture* MaskTexture,
	FRHITexture* DamageHudTexture,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// 마지막 콜백이면 엔진이 OverrideOutput에 써주길 기대하므로, 꺼져 있어도 그 경우엔
	// 세기 0 / HUD 0(원본 그대로 통과)으로 그린다.
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
			TEXT("RDG.PartnerKnockDown.Output"));
	}

	const FScreenPassTextureViewport InputViewport(SceneColor);
	const FIntPoint ViewSize = InputViewport.Rect.Size();
	FRDGTextureRef WeightTexture = AddMaskPass(GraphBuilder, View, ViewSize, Parameters, MaskTexture);

	const FVector2f InvExtent(
		1.0f / FMath::Max(1, InputViewport.Extent.X),
		1.0f / FMath::Max(1, InputViewport.Extent.Y));
	const FVector2f BilinearMinUV(
		(static_cast<float>(InputViewport.Rect.Min.X) + 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Min.Y) + 0.5f) * InvExtent.Y);
	const FVector2f BilinearMaxUV(
		(static_cast<float>(InputViewport.Rect.Max.X) - 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Max.Y) - 0.5f) * InvExtent.Y);

	// Tonemap 이후 장면색은 이미 sRGB 인코딩된 값이라, sRGB 텍스처는 샘플링 결과(선형)를 다시 인코딩해서 맞춘다.
	// 사망 비네트(FRDGDeathColorLerpPass)와 같은 판단.
	static const TConsoleVariableData<float>* TonemapperGamma = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.TonemapperGamma"));
	const bool bDefaultTonemapperGamma = !TonemapperGamma || TonemapperGamma->GetValueOnRenderThread() <= 0.0f;
	const bool bSDRSRGBOutput = View.Family && View.Family->RenderTarget
		&& View.Family->RenderTarget->GetDisplayOutputFormat() == EDisplayOutputFormat::SDR_sRGB;
	const bool bSRGBRenderTarget = Output.Texture && EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB);
	const bool bSRGBDamageHudTexture = DamageHudTexture && EnumHasAnyFlags(DamageHudTexture->GetDesc().Flags, TexCreate_SRGB);

	FRDGPartnerKnockDownPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGPartnerKnockDownPS::FParameters>();
	PassParameters->InputTexture = SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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

	PassParameters->WeightTexture = WeightTexture;
	PassParameters->WeightSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->bMaskVisualize = bEnabled && Parameters.bMaskVisualize ? 1 : 0;

	// HUD가 없으면 불투명도 0으로 셰이더에서 샘플링 자체를 건너뛴다.
	PassParameters->DamageHudTexture = DamageHudTexture ? DamageHudTexture : GBlackTexture->TextureRHI.GetReference();
	PassParameters->DamageHudSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->DamageHudTint = FVector3f(Parameters.DamageHud.Tint.R, Parameters.DamageHud.Tint.G, Parameters.DamageHud.Tint.B);
	PassParameters->DamageHudOpacity = bEnabled && DamageHudTexture ? GetDamageHudOpacity(Parameters) : 0.0f;
	PassParameters->DamageHudExponent = GetDamageHudExponent(Parameters);
	PassParameters->DamageHudIntensity = GetDamageHudIntensity(Parameters);
	PassParameters->DamageHudDarken = FMath::Clamp(Parameters.DamageHud.Darken, 0.0f, 1.0f);
	PassParameters->DamageHudBreath = GetDamageHudBreath(Parameters);
	PassParameters->DamageHudBlendMode = Parameters.DamageHud.BlendMode == static_cast<int32>(EPartnerKnockDownDamageHudBlend::AlphaOver)
		? static_cast<int32>(EPartnerKnockDownDamageHudBlend::AlphaOver)
		: static_cast<int32>(EPartnerKnockDownDamageHudBlend::AddDarken);
	PassParameters->bEncodeDamageHudToSRGB = bSDRSRGBOutput && bDefaultTonemapperGamma
		&& bSRGBDamageHudTexture && !bSRGBRenderTarget ? 1 : 0;

	PassParameters->Time = FMath::Max(0.0f, Parameters.Time);
	PassParameters->Intensity = bEnabled ? FMath::Max(0.0f, Parameters.MaxIntensity) : 0.0f;
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
	PassParameters->Tint = FVector3f(Parameters.Tint.R, Parameters.Tint.G, Parameters.Tint.B);

	// 렌즈 CA 배율은 UI CA와 같은 식으로 만든다. 꺼져 있으면 0이라 셰이더가 샘플링을 건너뛴다.
	const bool bChromatic = bEnabled && Parameters.bChromaticAberration && Parameters.ChromaticIntensity > 0.0f;
	const float ChromaticStartOffset = FMath::Clamp(Parameters.ChromaticStartOffset, 0.0f, 1.0f);
	PassParameters->ChromaticStartOffset = ChromaticStartOffset;
	PassParameters->ChromaticScale = bChromatic
		? FRDGUIChromaticAberrationPass::BuildChromaticScale(Parameters.ChromaticIntensity, ChromaticStartOffset)
		: FVector2f::ZeroVector;
	PassParameters->bChromaticGlitchOnly = Parameters.bChromaticGlitchOnly ? 1 : 0;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGPartnerKnockDownPS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.PartnerKnockDown"),
		View,
		FScreenPassTextureViewport(Output),
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
