#include "FRDGExplosionVolumeVisualizePass.h"

#include "FRDGExplosionVolumePass.h"
#include "RDGExplosionVolumeVisualizePS.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "SceneView.h"

namespace
{
TAutoConsoleVariable<int32> CVarExplosionVolumeVisualize(
	TEXT("rdg.ExplosionVolume.Visualize"),
	0,
	TEXT("Visualize RDG explosion volume. 0=off, 1=atlas falloff, 2=atlas velocity. Projection modes are disabled while the atlas path is stabilized."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarExplosionVolumeVisualizeOpacity(
	TEXT("rdg.ExplosionVolume.VisualizeOpacity"),
	0.85f,
	TEXT("Opacity for RDG explosion volume visualization."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarExplosionVolumeVisualizeScale(
	TEXT("rdg.ExplosionVolume.VisualizeScale"),
	0.45f,
	TEXT("Atlas size as a fraction of the smaller viewport dimension."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarExplosionVolumeVisualizeVelocityScale(
	TEXT("rdg.ExplosionVolume.VisualizeVelocityScale"),
	0.001f,
	TEXT("Scale used to map velocity values into debug color."),
	ECVF_RenderThreadSafe);
}

bool FRDGExplosionVolumeVisualizePass::IsEnabled()
{
	const int32 Mode = CVarExplosionVolumeVisualize.GetValueOnAnyThread();

	//UE_LOG(LogTemp, Error, TEXT("[Explosion] IsEnabled and the Value is %d"), Mode);

	return Mode == 1 || Mode == 2;
}

FScreenPassTexture FRDGExplosionVolumeVisualizePass::AddPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor,
	const FRDGTextureRef& CachedVolume,
	const FScreenPassRenderTarget& OverrideOutput)
{
	if (!SceneColor.IsValid() || !IsEnabled())
	{
		return SceneColor;
	}

	if (!CachedVolume)
	{
		return SceneColor;
	}

	FRDGTextureRef VolumeTexture = CachedVolume;
	const FIntVector TextureSize = FIntVector(
		RDGExplosionVolume::VolumeSize,
		RDGExplosionVolume::VolumeSize,
		RDGExplosionVolume::VolumeSize);

	FScreenPassRenderTarget FinalOutput = OverrideOutput;
	if (FinalOutput.IsValid())
	{
		const bool bPartialOutput =
			FinalOutput.ViewRect.Min != FIntPoint::ZeroValue ||
			(FinalOutput.Texture && FinalOutput.Texture->Desc.Extent != FinalOutput.ViewRect.Max);
		if (bPartialOutput)
		{
			FinalOutput.LoadAction = ERenderTargetLoadAction::ELoad;
		}
	}
	else
	{

		UE_LOG(LogTemp, Error, TEXT("[Explosion] !FinalOutput.IsValid()"));

		FinalOutput = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			ERenderTargetLoadAction::ENoAction,
			TEXT("RDG.ExplosionVolume.Visualize.Output"));
	}

	FRDGExplosionVolumeVisualizePS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FRDGExplosionVolumeVisualizePS::FParameters>();
	PassParameters->SceneColorTexture = SceneColor.Texture;
	PassParameters->VolumeTexture = VolumeTexture;
	PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->VolumeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->VisualizeSettings = FVector4f(
		static_cast<float>(CVarExplosionVolumeVisualize.GetValueOnRenderThread()),
		FMath::Clamp(CVarExplosionVolumeVisualizeOpacity.GetValueOnRenderThread(), 0.0f, 1.0f),
		FMath::Clamp(CVarExplosionVolumeVisualizeScale.GetValueOnRenderThread(), 0.05f, 0.95f),
		FMath::Max(CVarExplosionVolumeVisualizeVelocityScale.GetValueOnRenderThread(), 0.000001f));
	const FScreenPassTextureViewport InputViewport(SceneColor);
	PassParameters->TextureSettings = FVector4f(
		static_cast<float>(InputViewport.Rect.Width()),
		static_cast<float>(InputViewport.Rect.Height()),
		static_cast<float>(TextureSize.X),
		0.0f);
	const FVector2f InvExtent(
		1.0f / FMath::Max(1, InputViewport.Extent.X),
		1.0f / FMath::Max(1, InputViewport.Extent.Y));
	const FVector2f BilinearMinUV(
		(static_cast<float>(InputViewport.Rect.Min.X) + 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Min.Y) + 0.5f) * InvExtent.Y);
	const FVector2f BilinearMaxUV(
		(static_cast<float>(InputViewport.Rect.Max.X) - 0.5f) * InvExtent.X,
		(static_cast<float>(InputViewport.Rect.Max.Y) - 0.5f) * InvExtent.Y);
	PassParameters->ViewRectUV = FVector4f(
		BilinearMinUV.X,
		BilinearMinUV.Y,
		FMath::Max(BilinearMinUV.X, BilinearMaxUV.X),
		FMath::Max(BilinearMinUV.Y, BilinearMaxUV.Y));
	PassParameters->RenderTargets[0] = FinalOutput.GetRenderTargetBinding();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FRDGExplosionVolumeVisualizePS> PixelShader(ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("RDG.ExplosionVolume.Visualize"),
		View,
		FScreenPassTextureViewport(FinalOutput),
		FScreenPassTextureViewport(SceneColor),
		VertexShader,
		PixelShader,
		PassParameters);

	return FScreenPassTexture(FinalOutput);
}
