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
	FHeatHazeSourceShaderData& OutData)
{
	const FVector Center(Source.Transform.GetLocation());
	const FVector XAxis(Source.Transform.GetUnitAxis(EAxis::X));
	const FVector YAxis(Source.Transform.GetUnitAxis(EAxis::Y));
	const FVector HalfX = XAxis * static_cast<double>(Source.Size.X * 0.5f);
	const FVector HalfY = YAxis * static_cast<double>(Source.Size.Y * 0.5f);

	FVector2D CenterPixel;
	FVector2D XPixel;
	FVector2D YPixel;
	if (!View.WorldToPixel(Center, CenterPixel)
		|| !View.WorldToPixel(Center + HalfX, XPixel)
		|| !View.WorldToPixel(Center + HalfY, YPixel))
	{
		return false;
	}

	const FVector2f FieldScale(
		static_cast<float>(FieldExtent.X) / static_cast<float>(FMath::Max(1, SceneExtent.X)),
		static_cast<float>(FieldExtent.Y) / static_cast<float>(FMath::Max(1, SceneExtent.Y)));

	const FVector2f CenterInField(
		static_cast<float>(CenterPixel.X) * FieldScale.X,
		static_cast<float>(CenterPixel.Y) * FieldScale.Y);
	const FVector2f AxisXInField(
		static_cast<float>(XPixel.X - CenterPixel.X) * FieldScale.X,
		static_cast<float>(XPixel.Y - CenterPixel.Y) * FieldScale.Y);
	const FVector2f AxisYInField(
		static_cast<float>(YPixel.X - CenterPixel.X) * FieldScale.X,
		static_cast<float>(YPixel.Y - CenterPixel.Y) * FieldScale.Y);

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

	const FIntPoint SceneExtent = SceneColor.ViewRect.Size();
	for (const FHeatHazeSourceData& Source : Sources)
	{
		FHeatHazeSourceShaderData ShaderData;
		if (ProjectHeatHazeSource(View, FieldExtent, SceneExtent, Source, ShaderData))
		{
			ShaderSources.Add(ShaderData);
		}
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

FScreenPassTexture CompositeHeatHaze(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	FRDGTextureRef RefractionTexture,
	const FIntPoint& FieldExtent)
{
	FScreenPassRenderTarget Output = FScreenPassRenderTarget::CreateFromInput(
		GraphBuilder,
		SceneColor,
		ERenderTargetLoadAction::ENoAction,
		TEXT("RDG.HeatHaze.Output"));

	FRDGHeatHazeCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGHeatHazeCompositePS::FParameters>();
	PassParameters->SceneColorTexture = SceneColor.Texture;
	PassParameters->RefractionTexture = RefractionTexture;
	PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RefractionSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->FieldExtent = FVector2f(static_cast<float>(FieldExtent.X), static_cast<float>(FieldExtent.Y));

	FScreenPassTextureViewport InputViewport(SceneColor);
	const FVector2D RectToExtent = InputViewport.GetRectToExtentRatio();
	PassParameters->ViewRectMinUV = FVector2f(0.0f, 0.0f);
	PassParameters->ViewRectMaxUV = FVector2f(static_cast<float>(RectToExtent.X), static_cast<float>(RectToExtent.Y));
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

	return MoveTemp(Output);
}
}

FScreenPassTexture FRDGHeatHazePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	TConstArrayView<FHeatHazeSourceData> Sources)
{
	if (!SceneColor.IsValid() || Sources.IsEmpty())
	{
		return SceneColor;
	}

	const FIntPoint SceneExtent = SceneColor.ViewRect.Size();
	const FIntPoint FieldExtent(
		FMath::Max(1, SceneExtent.X / 2),
		FMath::Max(1, SceneExtent.Y / 2));

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
		FieldExtent);
}
