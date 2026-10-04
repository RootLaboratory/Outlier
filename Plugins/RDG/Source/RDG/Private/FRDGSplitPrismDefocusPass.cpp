#include "FRDGSplitPrismDefocusPass.h"

#include "FPostProcessStructures.h"
#include "GenerateMips.h"
#include "RDGSplitPrismDefocusPS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"

namespace
{
	// MaxRadius는 이 높이 기준 px다. 해상도가 바뀌어도 화면 대비 크기가 같게 맞춘다.
	constexpr float SplitPrismDefocusReferenceHeight = 1080.0f;

	// 4K, 최대 반경 64px, 샘플 8개여도 샘플 간격 밉은 6단계 안쪽이다.
	constexpr int32 SplitPrismDefocusMaxMipCount = 8;
}

FScreenPassTexture FRDGSplitPrismDefocusPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FSplitPrismDefocusParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	const FIntPoint ViewSize = SceneColor.ViewRect.Size();
	if (!SceneColor.IsValid() || !Parameters.bEnabled || ViewSize.X <= 0 || ViewSize.Y <= 0)
	{
		return SceneColor;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

	// 뷰 영역만 밉 체인이 있는 텍스처로 옮기고 밉을 만든다. 원판 샘플은 샘플 간격에 맞는 밉을 읽는다.
	const int32 MipCount = FMath::Min(
		static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(FMath::Max(ViewSize.X, ViewSize.Y)))) + 1,
		SplitPrismDefocusMaxMipCount);
	const FRDGTextureDesc PrefilterDesc = FRDGTextureDesc::Create2D(
		ViewSize,
		PF_FloatRGBA,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		static_cast<uint8>(MipCount));
	FRDGTextureRef PrefilterTexture = GraphBuilder.CreateTexture(
		PrefilterDesc,
		TEXT("RDG.SplitPrismDefocus.Prefilter"));

	FRDGDrawTextureInfo DrawInfo;
	DrawInfo.Size = ViewSize;
	DrawInfo.SourcePosition = SceneColor.ViewRect.Min;
	AddDrawTexturePass(GraphBuilder, ShaderMap, SceneColor.Texture, PrefilterTexture, DrawInfo);

	FGenerateMips::Execute(
		GraphBuilder,
		View.GetFeatureLevel(),
		PrefilterTexture,
		TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
		EGenerateMipsPass::Raster);

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
			TEXT("RDG.SplitPrismDefocus.Output"));
	}

	FRDGSplitPrismDefocusPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGSplitPrismDefocusPS::FParameters>();
	PassParameters->InputTexture = PrefilterTexture;
	PassParameters->InputSampler =
		TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->InputTexelSize = FVector2f(
		1.0f / static_cast<float>(ViewSize.X),
		1.0f / static_cast<float>(ViewSize.Y));
	PassParameters->Defocus = Parameters.Defocus;
	PassParameters->MaxRadius =
		Parameters.MaxRadius * static_cast<float>(ViewSize.Y) / SplitPrismDefocusReferenceHeight;
	PassParameters->FringeAmount = Parameters.FringeAmount;
	PassParameters->RimBias = Parameters.RimBias;
	PassParameters->SampleCount = static_cast<uint32>(FMath::Max(Parameters.SampleCount, 1));
	PassParameters->MaxMipLevel = static_cast<float>(MipCount - 1);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGSplitPrismDefocusPS> PixelShader(ShaderMap);

	// 입력 뷰포트를 밉 텍스처 전체로 잡아서 셰이더 UV가 0~1 = 뷰 영역이 되게 한다.
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.SplitPrismDefocus"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(PrefilterTexture),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}
