#include "RDGDualKawaseBlurPS.h"

#include "FPostProcessStructures.h"
#include "FRDGDualKawaseBlurPass.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"

bool FRDGDualKawaseBlurPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

IMPLEMENT_GLOBAL_SHADER(FRDGDualKawaseBlurPS, "/Plugin/RDG/DualKawaseBlur.usf", "MainPS", SF_Pixel);

static FRDGTextureRef CreateBlurTexture(FRDGBuilder& GraphBuilder, const FRDGTextureDesc& SourceDesc, const FIntPoint& Extent, const TCHAR* Name)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		Extent,
		SourceDesc.Format,
		FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);

	return GraphBuilder.CreateTexture(Desc, Name);
}

static FScreenPassTexture RenderDualKawaseStage(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& Input,
	FRDGTextureRef OutputTexture,
	const FIntRect& OutputRect,
	float KernelOffset,
	bool bUpsample,
	bool bCompositeWithOriginal,
	const FScreenPassTexture& OriginalTexture,
	float BlendWeight,
	const TCHAR* EventName)
{
	FScreenPassRenderTarget Output(OutputTexture, OutputRect, ERenderTargetLoadAction::ENoAction);

	FRDGDualKawaseBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDualKawaseBlurPS::FParameters>();
	PassParameters->InputTexture = Input.Texture;
	PassParameters->OriginalTexture = OriginalTexture.IsValid() ? OriginalTexture.Texture : Input.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FScreenPassTextureViewport InputViewport(Input);
	PassParameters->InvResolution = FVector2f(1.0f / FMath::Max(1, InputViewport.Extent.X), 1.0f / FMath::Max(1, InputViewport.Extent.Y));
	PassParameters->KernelOffset = KernelOffset;
	PassParameters->BlendWeight = BlendWeight;
	PassParameters->bUpsample = bUpsample ? 1u : 0u;
	PassParameters->bCompositeWithOriginal = bCompositeWithOriginal ? 1u : 0u;
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDualKawaseBlurPS> PixelShader(ShaderMap);

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

FScreenPassTexture FRDGDualKawaseBlurPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FDualKawaseBlurParameters& Parameters,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || Parameters.bEnabled == 0)
	{
		return SceneColor;
	}


	const int32 PassCount = FMath::Clamp(Parameters.DownsampleCount, 1, 6);
	const float Radius = FMath::Max(0.0f, Parameters.BlurRadius);
	TArray<FScreenPassTexture, TInlineAllocator<8>> DownsampleChain;
	DownsampleChain.Reserve(PassCount);

	FScreenPassTexture Current = SceneColor;
	for (int32 Index = 0; Index < PassCount; ++Index)
	{
		const FIntPoint Extent(
			FMath::Max(1, Current.ViewRect.Width() / 2),
			FMath::Max(1, Current.ViewRect.Height() / 2));

		FRDGTextureRef DownsampleTexture = CreateBlurTexture(
			GraphBuilder,
			SceneColor.Texture->Desc,
			Extent,
			*FString::Printf(TEXT("RDG.DualKawase.Downsample.%d"), Index));

		Current = RenderDualKawaseStage(
			GraphBuilder,
			View,
			Current,
			DownsampleTexture,
			FIntRect(FIntPoint::ZeroValue, Extent),
			Radius + Index,
			false,
			false,
			Current,
			1.0f,
			*FString::Printf(TEXT("RDG.DualKawase.Downsample.%d"), Index));

		DownsampleChain.Add(Current);
	}

	for (int32 Index = PassCount - 2; Index >= 0; --Index)
	{
		const FScreenPassTexture& TargetLevel = DownsampleChain[Index];
		FRDGTextureRef UpsampleTexture = CreateBlurTexture(
			GraphBuilder,
			SceneColor.Texture->Desc,
			TargetLevel.ViewRect.Size(),
			*FString::Printf(TEXT("RDG.DualKawase.Upsample.%d"), Index));

		Current = RenderDualKawaseStage(
			GraphBuilder,
			View,
			Current,
			UpsampleTexture,
			TargetLevel.ViewRect,
			Radius + Index,
			true,
			false,
			TargetLevel,
			1.0f,
			*FString::Printf(TEXT("RDG.DualKawase.Upsample.%d"), Index));
	}

	FScreenPassRenderTarget FinalOutput = OverrideOutput;
	if (!FinalOutput.IsValid())
	{
		FRDGTextureDesc FinalDesc = SceneColor.Texture->Desc;
		EnumRemoveFlags(FinalDesc.Flags, ETextureCreateFlags::Presentable);
		FinalDesc.Reset();
		FinalDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;

		FRDGTextureRef FinalTexture = GraphBuilder.CreateTexture(
			FinalDesc,
			TEXT("RDG.DualKawase.Final"));

		FinalOutput = FScreenPassRenderTarget(
			FinalTexture,
			SceneColor.ViewRect,
			ERenderTargetLoadAction::ENoAction);
	}

	return RenderDualKawaseStage(
		GraphBuilder,
		View,
		Current,
		FinalOutput.Texture,
		FinalOutput.ViewRect,
		0.0f,
		false,
		true,
		SceneColor,
		Parameters.BlendWeight,
		TEXT("RDG.DualKawase.Composite"));
}
