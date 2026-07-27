#include "FRDGPixelSortingPass.h"

#include "FPostProcessStructures.h"
#include "FRDGSceneColorCopyPass.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

namespace PixelSorting
{
	static constexpr uint32 DirectionColumn = 0u;
	static constexpr uint32 DirectionRow = 1u;

	template <typename TParameters>
	void SetCommonParameters(
		TParameters* PassParameters,
		const FScreenPassTexture& SceneColor,
		uint32 Direction,
		const FPixelSortingParameters& Parameters)
	{
		const int32 LineLength = Direction == DirectionRow
			? SceneColor.ViewRect.Width()
			: SceneColor.ViewRect.Height();

		PassParameters->ViewRectMin = SceneColor.ViewRect.Min;
		PassParameters->LineLength = static_cast<uint32>(LineLength);
		PassParameters->PaddedLength = FMath::RoundUpToPowerOfTwo(static_cast<uint32>(FMath::Max(1, LineLength)));
		PassParameters->Direction = Direction;
		PassParameters->Mode = static_cast<uint32>(FMath::Clamp(
			Parameters.Mode,
			static_cast<int32>(EPixelSortingMode::White),
			static_cast<int32>(EPixelSortingMode::Dark)));
		PassParameters->Threshold = FMath::RoundToInt(FMath::Clamp(Parameters.Threshold, 0.0f, 255.0f));
	}

	template <typename TParameters>
	void SetColorBlendParameters(TParameters* PassParameters, const FPixelSortingParameters& Parameters)
	{
		PassParameters->TargetColor = FVector3f(
			Parameters.TargetColor.R,
			Parameters.TargetColor.G,
			Parameters.TargetColor.B);
		PassParameters->ColorBlendProgress = FMath::Clamp(Parameters.Progress, 0.0f, 1.0f);
		PassParameters->bColorInterpolationEnabled = Parameters.bColorInterpolationEnabled != 0 ? 1u : 0u;
	}

	FIntVector GetGroupCount(const FScreenPassTexture& SceneColor, uint32 Direction)
	{
		return Direction == DirectionRow
			? FIntVector(1, SceneColor.ViewRect.Height(), 1)
			: FIntVector(SceneColor.ViewRect.Width(), 1, 1);
	}

	FRDGTextureRef CreatePackedTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			Extent,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		return GraphBuilder.CreateTexture(Desc, TEXT("RDG.PixelSorting.PackedIntermediate"));
	}

	FRDGTextureRef CreateOutputTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			Extent,
			PF_R8G8B8A8,
			FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

		return GraphBuilder.CreateTexture(Desc, TEXT("RDG.PixelSorting.Output"));
	}
}

class FPixelSortColorToPackedCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortColorToPackedCS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortColorToPackedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputPacked)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(uint32, LineLength)
		SHADER_PARAMETER(uint32, PaddedLength)
		SHADER_PARAMETER(uint32, Direction)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(int32, Threshold)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPixelSortPackedToColorCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortPackedToColorCS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortPackedToColorCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, InputPacked)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(uint32, LineLength)
		SHADER_PARAMETER(uint32, PaddedLength)
		SHADER_PARAMETER(uint32, Direction)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(int32, Threshold)
		SHADER_PARAMETER(FVector3f, TargetColor)
		SHADER_PARAMETER(float, ColorBlendProgress)
		SHADER_PARAMETER(uint32, bColorInterpolationEnabled)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPixelSortColorToColorCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortColorToColorCS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortColorToColorCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(uint32, LineLength)
		SHADER_PARAMETER(uint32, PaddedLength)
		SHADER_PARAMETER(uint32, Direction)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(int32, Threshold)
		SHADER_PARAMETER(FVector3f, TargetColor)
		SHADER_PARAMETER(float, ColorBlendProgress)
		SHADER_PARAMETER(uint32, bColorInterpolationEnabled)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPixelSortDownsampleBoxCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortDownsampleBoxCS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortDownsampleBoxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampleSource)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DownsampleOutput)
		SHADER_PARAMETER(FIntPoint, SourceExtent)
		SHADER_PARAMETER(FIntPoint, ReducedExtent)
		SHADER_PARAMETER(int32, ResolutionDivisor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPixelSortCoverageCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortCoverageCS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortCoverageCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CoverageSorted)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CoverageOriginal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, CoverageOutput)
		SHADER_PARAMETER(FIntPoint, ReducedExtent)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPixelSortCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelSortCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FPixelSortCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CompositeFullResTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CompositeSortedTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeSortedSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CompositeCoverageTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CompositeCoverageSampler)
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

IMPLEMENT_GLOBAL_SHADER(FPixelSortColorToPackedCS, "/Plugin/RDG/PixelSorting.usf", "ColorToPackedCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortPackedToColorCS, "/Plugin/RDG/PixelSorting.usf", "PackedToColorCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortColorToColorCS, "/Plugin/RDG/PixelSorting.usf", "ColorToColorCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortDownsampleBoxCS, "/Plugin/RDG/PixelSorting.usf", "DownsampleBoxCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortCoverageCS, "/Plugin/RDG/PixelSorting.usf", "CoverageCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortCompositePS, "/Plugin/RDG/PixelSorting.usf", "CompositePS", SF_Pixel);

namespace PixelSorting
{
	// 정렬 자체는 View에 의존하지 않음 — feature level만 있으면 됨.
	// 정렬된 텍스처를 돌려주고, OverrideOutput으로의 해상(resolve)은 호출부가 맡음.
	// 정렬을 건너뛰어야 하면 nullptr.
	FRDGTextureRef AddSortPasses(
		FRDGBuilder& GraphBuilder,
		ERHIFeatureLevel::Type FeatureLevel,
		const FScreenPassTexture& SceneColor,
		const FPixelSortingParameters& Parameters);

	// 원본 해상도에서 정렬할지, 축소본에서 정렬한 뒤 합성할지 결정해 전체 경로를
	// 처리함. 정렬이 아무것도 하지 않았으면 입력을 그대로 돌려줌.
	FScreenPassTexture AddSortedSceneColor(
		FRDGBuilder& GraphBuilder,
		ERHIFeatureLevel::Type FeatureLevel,
		const FScreenPassTexture& SceneColor,
		const FPixelSortingParameters& Parameters);

	int32 GetEffectiveDivisor(const FScreenPassTexture& SceneColor, const FPixelSortingParameters& Parameters)
	{
		const int32 Requested = FMath::Clamp(Parameters.ResolutionDivisor, 1, 8);

		// 축소 경로는 뷰포트가 텍스처 원점에서 시작한다고 가정함(backbuffer가 그럼).
		// 오프셋이 있는 뷰렉트는 좌표 매핑이 달라지므로 원본 해상도로 처리.
		if (SceneColor.ViewRect.Min != FIntPoint::ZeroValue)
		{
			return 1;
		}

		return Requested;
	}

	FRDGTextureRef CreateCoverageTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			Extent,
			PF_G8,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		return GraphBuilder.CreateTexture(Desc, TEXT("RDG.PixelSorting.Coverage"));
	}
}

FScreenPassTexture PixelSorting::AddSortedSceneColor(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const FScreenPassTexture& SceneColor,
	const FPixelSortingParameters& Parameters)
{
	const int32 Divisor = GetEffectiveDivisor(SceneColor, Parameters);

	if (Divisor <= 1)
	{
		FRDGTextureRef SortedTexture = AddSortPasses(GraphBuilder, FeatureLevel, SceneColor, Parameters);
		return SortedTexture
			? FScreenPassTexture(SortedTexture, SceneColor.ViewRect)
			: SceneColor;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	const FIntPoint SourceExtent = SceneColor.ViewRect.Size();
	const FIntPoint ReducedExtent = FIntPoint(
		FMath::DivideAndRoundUp(SourceExtent.X, Divisor),
		FMath::DivideAndRoundUp(SourceExtent.Y, Divisor));

	// 1) 박스 필터로 축소. 픽셀 하나를 고르면 얇은 UI 요소가 임계값을 들락날락하며
	//    깜빡이므로 반드시 평균을 씀.
	FRDGTextureRef ReducedTexture = CreateOutputTexture(GraphBuilder, ReducedExtent);
	{
		FPixelSortDownsampleBoxCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPixelSortDownsampleBoxCS::FParameters>();
		PassParameters->DownsampleSource = SceneColor.Texture;
		PassParameters->DownsampleOutput = GraphBuilder.CreateUAV(ReducedTexture);
		PassParameters->SourceExtent = SourceExtent;
		PassParameters->ReducedExtent = ReducedExtent;
		PassParameters->ResolutionDivisor = Divisor;

		TShaderMapRef<FPixelSortDownsampleBoxCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.PixelSorting.Downsample(1/%d)", Divisor),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ReducedExtent, FIntPoint(8, 8)));
	}

	// 2) 축소본에서 정렬.
	const FScreenPassTexture ReducedSceneColor(ReducedTexture, FIntRect(FIntPoint::ZeroValue, ReducedExtent));
	FRDGTextureRef SortedTexture = AddSortPasses(GraphBuilder, FeatureLevel, ReducedSceneColor, Parameters);
	if (!SortedTexture)
	{
		return SceneColor;
	}

	// 3) 정렬 전후를 비교해 실제로 바뀐 픽셀만 표시.
	FRDGTextureRef CoverageTexture = CreateCoverageTexture(GraphBuilder, ReducedExtent);
	{
		FPixelSortCoverageCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPixelSortCoverageCS::FParameters>();
		PassParameters->CoverageSorted = SortedTexture;
		PassParameters->CoverageOriginal = ReducedTexture;
		PassParameters->CoverageOutput = GraphBuilder.CreateUAV(CoverageTexture);
		PassParameters->ReducedExtent = ReducedExtent;

		TShaderMapRef<FPixelSortCoverageCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.PixelSorting.Coverage"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ReducedExtent, FIntPoint(8, 8)));
	}

	// 4) 원본 풀 해상도 위에 정렬된 부분만 확대해 합성.
	FRDGTextureDesc CompositeDesc = SceneColor.Texture->Desc;
	EnumRemoveFlags(CompositeDesc.Flags, ETextureCreateFlags::Presentable);
	CompositeDesc.Reset();
	CompositeDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

	FRDGTextureRef CompositeTexture = GraphBuilder.CreateTexture(
		CompositeDesc,
		TEXT("RDG.PixelSorting.Composite"));

	FScreenPassRenderTarget Output(
		CompositeTexture,
		SceneColor.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	FPixelSortCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPixelSortCompositePS::FParameters>();
	PassParameters->CompositeFullResTexture = SceneColor.Texture;
	PassParameters->CompositeSortedTexture = SortedTexture;
	PassParameters->CompositeSortedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->CompositeCoverageTexture = CoverageTexture;
	PassParameters->CompositeCoverageSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->SourceExtent = SourceExtent;
	PassParameters->ReducedExtent = ReducedExtent;
	PassParameters->ResolutionDivisor = Divisor;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FPixelSortCompositePS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.PixelSorting.Composite"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return MoveTemp(Output);
}

FRDGTextureRef PixelSorting::AddSortPasses(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const FScreenPassTexture& SceneColor,
	const FPixelSortingParameters& Parameters)
{
	const bool bRowsRequested = Parameters.bSortRows != 0;
	const bool bColumnsRequested = Parameters.bSortColumns != 0;
	const bool bSortRows = bRowsRequested && SceneColor.ViewRect.Width() <= FRDGPixelSortingPass::MaxLineLength;
	const bool bSortColumns = bColumnsRequested && SceneColor.ViewRect.Height() <= FRDGPixelSortingPass::MaxLineLength;

	if (bRowsRequested && !bSortRows)
	{
		ensureMsgf(false, TEXT("Pixel Sorting row length %d exceeds the %d-pixel LDS limit."), SceneColor.ViewRect.Width(), FRDGPixelSortingPass::MaxLineLength);
	}
	if (bColumnsRequested && !bSortColumns)
	{
		ensureMsgf(false, TEXT("Pixel Sorting column length %d exceeds the %d-pixel LDS limit."), SceneColor.ViewRect.Height(), FRDGPixelSortingPass::MaxLineLength);
	}
	if (!bSortRows && !bSortColumns)
	{
		return nullptr;
	}

	FRDGTextureRef OutputTexture = PixelSorting::CreateOutputTexture(GraphBuilder, SceneColor.Texture->Desc.Extent);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	if (bSortRows && bSortColumns)
	{
		// Match the Processing source: columns first, then rows. Keep the intermediate in
		// packed ARGB8 so the second pass does not repack UNORM float samples.
		FRDGTextureRef PackedIntermediate = PixelSorting::CreatePackedTexture(GraphBuilder, SceneColor.Texture->Desc.Extent);

		FPixelSortColorToPackedCS::FParameters* ColumnParameters = GraphBuilder.AllocParameters<FPixelSortColorToPackedCS::FParameters>();
		ColumnParameters->InputColor = SceneColor.Texture;
		ColumnParameters->OutputPacked = GraphBuilder.CreateUAV(PackedIntermediate);
		PixelSorting::SetCommonParameters(ColumnParameters, SceneColor, PixelSorting::DirectionColumn, Parameters);

		TShaderMapRef<FPixelSortColorToPackedCS> ColumnShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.PixelSorting.Columns"),
			ColumnShader,
			ColumnParameters,
			PixelSorting::GetGroupCount(SceneColor, PixelSorting::DirectionColumn));

		FPixelSortPackedToColorCS::FParameters* RowParameters = GraphBuilder.AllocParameters<FPixelSortPackedToColorCS::FParameters>();
		RowParameters->InputPacked = PackedIntermediate;
		RowParameters->OutputColor = GraphBuilder.CreateUAV(OutputTexture);
		PixelSorting::SetCommonParameters(RowParameters, SceneColor, PixelSorting::DirectionRow, Parameters);
		PixelSorting::SetColorBlendParameters(RowParameters, Parameters);

		TShaderMapRef<FPixelSortPackedToColorCS> RowShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RDG.PixelSorting.Rows"),
			RowShader,
			RowParameters,
			PixelSorting::GetGroupCount(SceneColor, PixelSorting::DirectionRow));
	}
	else
	{
		const uint32 Direction = bSortColumns ? PixelSorting::DirectionColumn : PixelSorting::DirectionRow;
		FPixelSortColorToColorCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPixelSortColorToColorCS::FParameters>();
		PassParameters->InputColor = SceneColor.Texture;
		PassParameters->OutputColor = GraphBuilder.CreateUAV(OutputTexture);
		PixelSorting::SetCommonParameters(PassParameters, SceneColor, Direction, Parameters);
		PixelSorting::SetColorBlendParameters(PassParameters, Parameters);

		TShaderMapRef<FPixelSortColorToColorCS> ComputeShader(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			Direction == PixelSorting::DirectionColumn
				? RDG_EVENT_NAME("RDG.PixelSorting.Columns")
				: RDG_EVENT_NAME("RDG.PixelSorting.Rows"),
			ComputeShader,
			PassParameters,
			PixelSorting::GetGroupCount(SceneColor, Direction));
	}

	return OutputTexture;
}

FScreenPassTexture FRDGPixelSortingPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FPixelSortingParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0)
	{
		return SceneColor;
	}

	const FScreenPassTexture SortedSceneColor = PixelSorting::AddSortedSceneColor(
		GraphBuilder,
		View.GetFeatureLevel(),
		SceneColor,
		Parameters);

	if (SortedSceneColor.Texture == SceneColor.Texture)
	{
		return SceneColor;
	}

	if (OverrideOutput.IsValid())
	{
		// Keep the compute output shader-readable, then resolve/convert it into the final
		// post-process target supplied by the game viewport.
		return FRDGSceneColorCopyPass::AddPass(
			GraphBuilder,
			View,
			SortedSceneColor,
			OverrideOutput);
	}

	return SortedSceneColor;
}

FScreenPassTexture FRDGPixelSortingPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassTexture& SceneColor,
	const FPixelSortingParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0)
	{
		return SceneColor;
	}

	const FScreenPassTexture SortedSceneColor = PixelSorting::AddSortedSceneColor(
		GraphBuilder,
		GMaxRHIFeatureLevel,
		SceneColor,
		Parameters);

	if (SortedSceneColor.Texture == SceneColor.Texture)
	{
		return SceneColor;
	}

	if (OverrideOutput.IsValid())
	{
		return FRDGSceneColorCopyPass::AddPass(
			GraphBuilder,
			SortedSceneColor,
			OverrideOutput);
	}

	return SortedSceneColor;
}
