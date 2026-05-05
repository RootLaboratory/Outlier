#include "OutlierPostProcessSceneViewExtension.h"
#include "FRDGDualKawaseBlurPass.h"
#include "FRDGExplosionVolumePass.h"
#include "FRDGHeatHazePass.h"
#include "FRDGMotionBlurPass.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"

FOutlierPostProcessSceneViewExtension::FOutlierPostProcessSceneViewExtension(const FAutoRegister& AutoRegister, ULocalPlayer* InLocalPlayer)
	: FSceneViewExtensionBase(AutoRegister)
	, LocalPlayer(InLocalPlayer)
{
}

void FOutlierPostProcessSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FOutlierPostProcessSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
}

void FOutlierPostProcessSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (ULocalPlayer* LP = LocalPlayer.Get())
	{
		if (UWorld* World = LP->GetWorld())
		{
			if (URDGEffectSourceWorldSubsystem* SourceSubsystem = World->GetSubsystem<URDGEffectSourceWorldSubsystem>())
			{
				TArray<FHeatHazeSourceData> HeatHazeSources;
				SourceSubsystem->GatherHeatHazeSources(HeatHazeSources);
				UpdateHeatHazeSources(HeatHazeSources);
			}
		}
	}

	if (!ShouldRenderAnyEffect())
	{
		return;
	}

	for (auto* View : InViewFamily.Views)
	{
		if (!View)
		{
			continue;
		}

		if (!IsTargetLocalPlayerView(*View))
		{
			continue;
		}
	}
}

bool FOutlierPostProcessSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return LocalPlayer.IsValid();
}

void FOutlierPostProcessSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (!bIsPassEnabled)
	{
		return;
	}

	if (!ShouldRenderAnyEffect())
	{
		return;
	}

	if (!IsTargetLocalPlayerView(View))
	{
		return;
	}

	if (PassId == EPostProcessingPass::MotionBlur && CachedParameters.MotionBlur.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::MotionBlurCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::BeforeDOF && HasHeatHazeSources())
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::HeatHazeCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DualKawaseBlur.bEnabled)
	{

		UE_LOG(LogTemp, Error, TEXT("KAWASE IN"));


		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DualKawaseBlurCallback_RenderThread));
	}
}

void FOutlierPostProcessSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	if (!IsTargetLocalPlayerView(InView) || !Inputs.SceneTextures)
	{
		return;
	}

	FRDGTextureRef SceneDepthTexture = (*Inputs.SceneTextures)->SceneDepthTexture;
	if (!SceneDepthTexture)
	{
		return;
	}

	FExplosionVolumeParameters ExplosionParameters;
	FRDGExplosionVolumePass::AddPass(
		GraphBuilder,
		InView,
		SceneDepthTexture,
		ExplosionParameters);
}

void FOutlierPostProcessSceneViewExtension::UpdateCachedUIParameters(const FPostProcessStrctureUI& InParameters)
{
	CachedUIParameters = InParameters;
}

void FOutlierPostProcessSceneViewExtension::UpdateCachedParameters(const FPostProcessStrcture& InParameters)
{
	CachedParameters = InParameters;
}

void FOutlierPostProcessSceneViewExtension::UpdateHeatHazeSources(const TArray<FHeatHazeSourceData>& InSources)
{
	FScopeLock Lock(&HeatHazeSourcesCriticalSection);
	CachedHeatHazeSources = InSources;
}

bool FOutlierPostProcessSceneViewExtension::ShouldRenderAnyEffect() const
{
	return CachedParameters.MotionBlur.bEnabled
		|| CachedParameters.LensFlare.bEnabled
		|| CachedParameters.BloomBlur.bEnabled
		|| CachedParameters.DualKawaseBlur.bEnabled
		|| HasHeatHazeSources();
}

bool FOutlierPostProcessSceneViewExtension::IsTargetLocalPlayerView(const FSceneView& InView) const
{

	if (!InView.Family)
	{
		return false;
	}

	return LocalPlayer.IsValid();
}

bool FOutlierPostProcessSceneViewExtension::HasHeatHazeSources() const
{
	FScopeLock Lock(&HeatHazeSourcesCriticalSection);
	return !CachedHeatHazeSources.IsEmpty();
}

void FOutlierPostProcessSceneViewExtension::CopyHeatHazeSources(TArray<FHeatHazeSourceData>& OutSources) const
{
	FScopeLock Lock(&HeatHazeSourcesCriticalSection);
	OutSources = CachedHeatHazeSources;
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::MotionBlurCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor),
		Inputs.OverrideOutput);

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGMotionBlurPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.MotionBlur);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DualKawaseBlurCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor),
		Inputs.OverrideOutput);

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGDualKawaseBlurPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.DualKawaseBlur);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::HeatHazeCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor),
		Inputs.OverrideOutput);

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	TArray<FHeatHazeSourceData> HeatHazeSources;
	CopyHeatHazeSources(HeatHazeSources);
	if (HeatHazeSources.IsEmpty())
	{
		return SceneColor;
	}

	return FRDGHeatHazePass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		HeatHazeSources);
}



