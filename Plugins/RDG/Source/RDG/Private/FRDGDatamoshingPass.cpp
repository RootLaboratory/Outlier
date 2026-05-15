#pragma once

#include "FRDGDatamoshingPass.h"
#include "FPostProcessStructures.h"
#include "RDGDatamoshingPS.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"
#include "RenderGraphUtils.h"  // AddCopyTexturePass

FScreenPassTexture FRDGDatamoshingPass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& Input,
	const FDatamoshingParameters& Parameters,
	TRefCountPtr<IPooledRenderTarget>& InOutHistoryRT,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!Input.IsValid())
	{
		InOutHistoryRT.SafeRelease();
		return Input;
	}

	// 1. 항상 우리 내부 텍스처에 그림 (history 무오염 보장)
	FRDGTextureDesc InternalDesc = Input.Texture->Desc;
	InternalDesc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;

	FRDGTextureRef InternalOutput = GraphBuilder.CreateTexture(
		InternalDesc,
		TEXT("RDG.Datamosh.Internal"));

	FScreenPassRenderTarget Output(
		InternalOutput,
		Input.ViewRect,
		ERenderTargetLoadAction::ENoAction);

	// 2. History 바인딩 (해상도 불일치 방어)
	FRDGTextureRef HistoryTexture = Input.Texture;  // Fallback
	if (InOutHistoryRT.IsValid() && InOutHistoryRT->GetDesc().Extent == Input.Texture->Desc.Extent)
	{
		HistoryTexture = GraphBuilder.RegisterExternalTexture(InOutHistoryRT);
	}

	// 3. 파라미터
	FRDGDatamoshingPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRDGDatamoshingPS::FParameters>();
	PassParameters->CurrentFrameTexture = Input.Texture;
	PassParameters->HistoryFrameTexture = HistoryTexture;
	PassParameters->Progress = Parameters.Progress;
	PassParameters->GlobalSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// 4. 셰이더
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGDatamoshingPS> PixelShader(ShaderMap);

	// 5. Draw → InternalOutput에 그림
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.Datamosh_DeathSequence"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Input),
		VertexShader,
		PixelShader,
		PassParameters);

	// 6. **우리 패스 결과만** history로 추출 (후속 패스 영향 받기 전)
	GraphBuilder.QueueTextureExtraction(InternalOutput, &InOutHistoryRT);

	// 7. OverrideOutput 있으면 결과를 거기로 복사하고 그걸 반환
	if (OverrideOutput.IsValid())
	{
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = FIntVector(Input.ViewRect.Width(), Input.ViewRect.Height(), 1);
		CopyInfo.SourcePosition = FIntVector(Input.ViewRect.Min.X, Input.ViewRect.Min.Y, 0);
		CopyInfo.DestPosition = FIntVector(OverrideOutput.ViewRect.Min.X, OverrideOutput.ViewRect.Min.Y, 0);

		AddCopyTexturePass(GraphBuilder, InternalOutput, OverrideOutput.Texture, CopyInfo);
		return OverrideOutput;
	}

	return MoveTemp(Output);
}
