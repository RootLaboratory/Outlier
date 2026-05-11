#include "FRDGHeatHazePass.h"

#include "HeatHazeSourceComponent.h"
#include "RDGHeatHazeCompositePS.h"
#include "RDGHeatHazeCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneView.h"

namespace
{
	constexpr int32 HeatHazeThreadGroupSize = 8;

	struct FHeatHazeSourceShaderData
	{
		FVector4f CenterAndStrength;
		FVector4f AxisXAndSoftness;
		FVector4f AxisYAndNoise;
		FVector4f NoiseDirectionAndPadding;
	};

	bool ProjectHeatHazeSource(
		const FSceneView& View,
		const FIntPoint& FieldExtent,
		const FIntPoint& SceneExtent,
		const FHeatHazeSourceData& Source,
		FHeatHazeSourceShaderData& OutData
	)
	{
		const FVector Center(Source.Transform.GetLocation());
		FVector2D CenterPixel;
		if (!View.WorldToPixel(Center, CenterPixel))
		{
			return false;
		}

		// AddDrawScreenPass UV = AbsolutePixel / SceneExtent, so RefractionTexture is sampled
		// at UV * FieldExtent = AbsolutePixel * FieldExtent / SceneExtent.
		// CenterPixel from WorldToPixel is already in absolute pixel space (includes ViewRect.Min),
		// so CenterInField must use the same scale — no ViewRect.Min subtraction needed.
		const FVector2f FieldScale(
			static_cast<float>(FieldExtent.X) / static_cast<float>(SceneExtent.X),
			static_cast<float>(FieldExtent.Y) / static_cast<float>(SceneExtent.Y));

		const FVector2f CenterInField(
			static_cast<float>(CenterPixel.X) * FieldScale.X,
			static_cast<float>(CenterPixel.Y) * FieldScale.Y);

		// Transform 축을 스크린에 투영 (빌보딩은 호출자 Transform에서 처리됨)
		// FTransform3f는 GetUnitAxis 없음 → 쿼터니언에서 직접 축 추출
		const FVector AxisXWorld(Source.Transform.GetRotation().GetAxisY());
		const FVector AxisYWorld(Source.Transform.GetRotation().GetAxisZ());

		FVector2D AxisXEndPixel, AxisYEndPixel;
		View.WorldToPixel(Center + AxisXWorld * Source.Size.X * 0.5f, AxisXEndPixel);
		View.WorldToPixel(Center + AxisYWorld * Source.Size.Y * 0.5f, AxisYEndPixel);

		// 차분에서는 ViewRect.Min이 상쇄됨
		const FVector2f AxisXInField(
			(AxisXEndPixel.X - CenterPixel.X) * FieldScale.X,
			(AxisXEndPixel.Y - CenterPixel.Y) * FieldScale.Y);
		const FVector2f AxisYInField(
			(AxisYEndPixel.X - CenterPixel.X) * FieldScale.X,
			(AxisYEndPixel.Y - CenterPixel.Y) * FieldScale.Y);

		if (AxisXInField.SizeSquared() < 1.0f || AxisYInField.SizeSquared() < 1.0f)
		{
			return false;
		}

		const float PixelOffsetScale = (FieldScale.X + FieldScale.Y) * 0.5f;
		OutData.CenterAndStrength = FVector4f(
			CenterInField.X,
			CenterInField.Y,
			FMath::Max(0.0f, Source.Strength),
			FMath::Max(0.0f, Source.MaxPixelOffset * PixelOffsetScale));
		OutData.AxisXAndSoftness = FVector4f(
			AxisXInField.X,
			AxisXInField.Y,
			FMath::Clamp(Source.EdgeSoftness, 0.001f, 1.0f),
			FMath::Max(0.001f, Source.NoiseScale));
		OutData.AxisYAndNoise = FVector4f(
			AxisYInField.X,
			AxisYInField.Y,
			FMath::Max(0.0f, Source.NoiseSpeed),
			FMath::Max(0.001f, Source.FalloffExponent));
		OutData.NoiseDirectionAndPadding = FVector4f(
			Source.NoiseDirection.X,
			Source.NoiseDirection.Y,
			0.0f,
			0.0f);
		return true;
	}
	FRDGTextureRef BuildHeatHazeRefractionField(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor,
		TConstArrayView<FHeatHazeSourceData> Sources,
		const FIntPoint& FieldExtent)
	{
		TArray<FHeatHazeSourceShaderData, TInlineAllocator<16>> ShaderSources;
		ShaderSources.Reserve(Sources.Num());

		for (const FHeatHazeSourceData& Source : Sources)
		{
			FHeatHazeSourceShaderData ShaderData;
			const FIntPoint SceneExtent = SceneColor.Texture->Desc.Extent;
			if (ProjectHeatHazeSource(View, FieldExtent, SceneExtent, Source, ShaderData))
			{
				ShaderSources.Add(ShaderData);
			}

			/*UE_LOG(LogTemp, Warning, TEXT("UnscaledViewRect: Min(%d,%d) Max(%d,%d) Size(%d,%d)"),
				View.UnscaledViewRect.Min.X, View.UnscaledViewRect.Min.Y,
				View.UnscaledViewRect.Max.X, View.UnscaledViewRect.Max.Y,
				View.UnscaledViewRect.Width(), View.UnscaledViewRect.Height());

			UE_LOG(LogTemp, Warning, TEXT("SceneColor ViewRect: Min(%d,%d) Max(%d,%d) Size(%d,%d)"),
				SceneColor.ViewRect.Min.X, SceneColor.ViewRect.Min.Y,
				SceneColor.ViewRect.Max.X, SceneColor.ViewRect.Max.Y,
				SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height());

			UE_LOG(LogTemp, Warning, TEXT("SceneColor Extent: %d, %d"),
				SceneColor.Texture->Desc.Extent.X, SceneColor.Texture->Desc.Extent.Y);*/

		}

		if (ShaderSources.IsEmpty())
		{
			return nullptr;
		}

		FRDGTextureDesc RefractionDesc = FRDGTextureDesc::Create2D(
			FieldExtent,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef RefractionTexture = GraphBuilder.CreateTexture(
			RefractionDesc,
			TEXT("RDG.HeatHaze.RefractionField"));

		FRDGBufferRef SourceBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("RDG.HeatHaze.SourceData"),
			sizeof(FHeatHazeSourceShaderData),
			ShaderSources.Num(),
			ShaderSources.GetData(),
			ShaderSources.Num() * sizeof(FHeatHazeSourceShaderData));

		FRDGHeatHazeFieldCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGHeatHazeFieldCS::FParameters>();
		PassParameters->SourceDataBuffer = GraphBuilder.CreateSRV(SourceBuffer);
		PassParameters->SourceCount = static_cast<uint32>(ShaderSources.Num());
		PassParameters->OutputExtent = FVector2f(static_cast<float>(FieldExtent.X), static_cast<float>(FieldExtent.Y));
		PassParameters->Time = View.Family ? static_cast<float>(View.Family->Time.GetWorldTimeSeconds()) : 0.0f;
		PassParameters->OutRefractionTexture = GraphBuilder.CreateUAV(RefractionTexture);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
		TShaderMapRef<FRDGHeatHazeFieldCS> ComputeShader(ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.HeatHaze.Field"),
			ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FIntVector(
				FMath::DivideAndRoundUp(FieldExtent.X, HeatHazeThreadGroupSize),
				FMath::DivideAndRoundUp(FieldExtent.Y, HeatHazeThreadGroupSize),
				1));

		return RefractionTexture;
	}
}
FScreenPassTexture CompositeHeatHaze(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	FRDGTextureRef RefractionTexture,
	const FIntPoint& FieldExtent,
	const FScreenPassRenderTarget& Output)
{
	FRDGHeatHazeCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGHeatHazeCompositePS::FParameters>();
	PassParameters->SceneColorTexture = SceneColor.Texture;
	PassParameters->RefractionTexture = RefractionTexture;
	PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RefractionSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->FieldExtent = FVector2f(static_cast<float>(FieldExtent.X), static_cast<float>(FieldExtent.Y));
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGHeatHazeCompositePS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.HeatHaze.Composite"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return FScreenPassTexture(Output);
}


FScreenPassTexture FRDGHeatHazePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	TConstArrayView<FHeatHazeSourceData> Sources,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Sources.IsEmpty())
	{
		return SceneColor;
	}

	// OverrideOutput이 유효하지 않으면 SceneColor 기반으로 출력 RT 생성
	const FScreenPassRenderTarget FinalOutput = OverrideOutput.IsValid()
		? OverrideOutput
		: FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, ERenderTargetLoadAction::ENoAction, TEXT("RDG.HeatHaze.Output"));

	const FIntPoint FieldExtent = FinalOutput.Texture->Desc.Extent;

	FRDGTextureRef RefractionTexture = BuildHeatHazeRefractionField(
		GraphBuilder,
		View,
		SceneColor,
		Sources,
		FieldExtent);

	if (!RefractionTexture)
	{
		return SceneColor;
	}

	return CompositeHeatHaze(
		GraphBuilder,
		View,
		SceneColor,
		RefractionTexture,
		FieldExtent,
		FinalOutput);
}

