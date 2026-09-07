#include "FRDGZoomBlurPass.h"

#include "FPostProcessStructures.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

class FZoomBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FZoomBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FZoomBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ZoomBlurInput)
		SHADER_PARAMETER_SAMPLER(SamplerState, ZoomBlurInputSampler)
		SHADER_PARAMETER(FVector2f, ZoomBlurCenter)
		SHADER_PARAMETER(float, ZoomBlurStrength)
		SHADER_PARAMETER(float, ZoomBlurStartOffset)
		SHADER_PARAMETER(float, ZoomBlurBlackFlushAlpha)
		SHADER_PARAMETER(float, ZoomBlurAspectRatio)
		SHADER_PARAMETER(int32, ZoomBlurSampleCount)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FZoomBlurCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FZoomBlurCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FZoomBlurCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CompositeFullResTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeFullResSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CompositeBlurredTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeBlurredSampler)
		SHADER_PARAMETER(FVector2f, ZoomBlurCenter)
		SHADER_PARAMETER(float, ZoomBlurStartOffset)
		SHADER_PARAMETER(float, ZoomBlurBlackFlushAlpha)
		SHADER_PARAMETER(float, ZoomBlurAspectRatio)
		SHADER_PARAMETER(FIntPoint, SourceExtent)
		SHADER_PARAMETER(FIntPoint, ReducedExtent)
		SHADER_PARAMETER(int32, ResolutionDivisor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FZoomBlurPS, "/Plugin/RDG/ZoomBlur.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FZoomBlurCompositePS, "/Plugin/RDG/ZoomBlur.usf", "CompositePS", SF_Pixel);

namespace ZoomBlur
{
	// 화면 중심 고정.
	static const FVector2f Center(0.5f, 0.5f);

	FRDGTextureRef CreateColorTexture(FRDGBuilder& GraphBuilder, const FRDGTextureDesc& TemplateDesc, FIntPoint Extent, const TCHAR* Name)
	{
		FRDGTextureDesc Desc = TemplateDesc;
		EnumRemoveFlags(Desc.Flags, ETextureCreateFlags::Presentable);
		Desc.Reset();
		Desc.Extent = Extent;
		Desc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

		return GraphBuilder.CreateTexture(Desc, Name);
	}

	int32 GetEffectiveDivisor(const FScreenPassTexture& Input, const FZoomBlurParameters& Parameters)
	{
		const int32 Requested = FMath::Clamp(Parameters.ResolutionDivisor, 1, 8);

		// 축소 경로는 뷰포트가 텍스처 원점에서 시작한다고 가정함(backbuffer가 그럼).
		if (Input.ViewRect.Min != FIntPoint::ZeroValue)
		{
			return 1;
		}

		return Requested;
	}
}

FScreenPassTexture FRDGZoomBlurPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& Input,
	const FZoomBlurParameters& Parameters)
{
	if (!Input.IsValid() || Parameters.bEnabled == 0)
	{
		return Input;
	}

	const float Strength = FMath::Max(0.0f, Parameters.Strength);
	const float BlackFlushAlpha = FMath::Clamp(Parameters.BlackFlushAlpha, 0.0f, 1.0f);
	const int32 SampleCount = FMath::Clamp(Parameters.SampleCount, 2, 64);
	if (Strength <= KINDA_SMALL_NUMBER && BlackFlushAlpha <= KINDA_SMALL_NUMBER)
	{
		return Input;
	}

	const FIntPoint SourceExtent = Input.ViewRect.Size();
	if (SourceExtent.X <= 0 || SourceExtent.Y <= 0)
	{
		return Input;
	}

	const float AspectRatio = static_cast<float>(SourceExtent.X) / static_cast<float>(FMath::Max(1, SourceExtent.Y));
	const float StartOffset = FMath::Clamp(Parameters.StartOffset, 0.0f, 0.99f);
	const int32 Divisor = ZoomBlur::GetEffectiveDivisor(Input, Parameters);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FZoomBlurPS> BlurPixelShader(ShaderMap);

	const FIntPoint ReducedExtent = FIntPoint(
		FMath::DivideAndRoundUp(SourceExtent.X, Divisor),
		FMath::DivideAndRoundUp(SourceExtent.Y, Divisor));

	// 축소 경로에서도 원본 풀 해상도를 그대로 샘플링함. 탭을 여러 개 평균내는
	// 게더라 축소 해상도로 쓰더라도 별도 다운샘플 패스가 필요 없음.
	FRDGTextureRef BlurTexture = ZoomBlur::CreateColorTexture(
		GraphBuilder,
		Input.Texture->Desc,
		ReducedExtent,
		TEXT("RDG.ZoomBlur.Blurred"));

	FScreenPassRenderTarget BlurOutput(
		BlurTexture,
		FIntRect(FIntPoint::ZeroValue, ReducedExtent),
		ERenderTargetLoadAction::ENoAction);

	{
		FZoomBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FZoomBlurPS::FParameters>();
		PassParameters->ZoomBlurInput = Input.Texture;
		PassParameters->ZoomBlurInputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ZoomBlurCenter = ZoomBlur::Center;
		PassParameters->ZoomBlurStrength = Strength;
		PassParameters->ZoomBlurStartOffset = StartOffset;
		// The downsampled path applies the black transition once in its full-resolution
		// composite pass. The full-resolution path has no composite pass, so apply it here.
		PassParameters->ZoomBlurBlackFlushAlpha = Divisor <= 1 ? BlackFlushAlpha : 0.0f;
		PassParameters->ZoomBlurAspectRatio = AspectRatio;
		PassParameters->ZoomBlurSampleCount = SampleCount;
		PassParameters->RenderTargets[0] = BlurOutput.GetRenderTargetBinding();

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.ZoomBlur(1/%d, %d taps)", Divisor, SampleCount),
			FScreenPassViewInfo(),
			FScreenPassTextureViewport(BlurOutput),
			FScreenPassTextureViewport(Input),
			VertexShader,
			BlurPixelShader,
			PassParameters);
	}

	if (Divisor <= 1)
	{
		return MoveTemp(BlurOutput);
	}

	FRDGTextureRef CompositeTexture = ZoomBlur::CreateColorTexture(
		GraphBuilder,
		Input.Texture->Desc,
		Input.Texture->Desc.Extent,
		TEXT("RDG.ZoomBlur.Composite"));

	FScreenPassRenderTarget CompositeOutput(
		CompositeTexture,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FZoomBlurCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FZoomBlurCompositePS::FParameters>();
	PassParameters->CompositeFullResTexture = Input.Texture;
	PassParameters->CompositeFullResSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->CompositeBlurredTexture = BlurTexture;
	PassParameters->CompositeBlurredSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ZoomBlurCenter = ZoomBlur::Center;
	PassParameters->ZoomBlurStartOffset = StartOffset;
	PassParameters->ZoomBlurBlackFlushAlpha = BlackFlushAlpha;
	PassParameters->ZoomBlurAspectRatio = AspectRatio;
	PassParameters->SourceExtent = SourceExtent;
	PassParameters->ReducedExtent = ReducedExtent;
	PassParameters->ResolutionDivisor = Divisor;
	PassParameters->RenderTargets[0] = CompositeOutput.GetRenderTargetBinding();

	TShaderMapRef<FZoomBlurCompositePS> CompositePixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.ZoomBlur.Composite"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(CompositeOutput),
		FScreenPassTextureViewport(Input),
		VertexShader,
		CompositePixelShader,
		PassParameters);

	return MoveTemp(CompositeOutput);
}
