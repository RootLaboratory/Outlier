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
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPixelSortColorToPackedCS, "/Plugin/RDG/PixelSorting.usf", "ColorToPackedCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortPackedToColorCS, "/Plugin/RDG/PixelSorting.usf", "PackedToColorCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPixelSortColorToColorCS, "/Plugin/RDG/PixelSorting.usf", "ColorToColorCS", SF_Compute);

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

	const bool bRowsRequested = Parameters.bSortRows != 0;
	const bool bColumnsRequested = Parameters.bSortColumns != 0;
	const bool bSortRows = bRowsRequested && SceneColor.ViewRect.Width() <= MaxLineLength;
	const bool bSortColumns = bColumnsRequested && SceneColor.ViewRect.Height() <= MaxLineLength;

	if (bRowsRequested && !bSortRows)
	{
		ensureMsgf(false, TEXT("Pixel Sorting row length %d exceeds the %d-pixel LDS limit."), SceneColor.ViewRect.Width(), MaxLineLength);
	}
	if (bColumnsRequested && !bSortColumns)
	{
		ensureMsgf(false, TEXT("Pixel Sorting column length %d exceeds the %d-pixel LDS limit."), SceneColor.ViewRect.Height(), MaxLineLength);
	}
	if (!bSortRows && !bSortColumns)
	{
		return SceneColor;
	}

	FRDGTextureRef OutputTexture = PixelSorting::CreateOutputTexture(GraphBuilder, SceneColor.Texture->Desc.Extent);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

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

	const FScreenPassTexture SortedSceneColor(OutputTexture, SceneColor.ViewRect);
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
