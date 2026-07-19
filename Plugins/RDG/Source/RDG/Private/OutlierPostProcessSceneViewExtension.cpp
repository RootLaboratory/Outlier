#include "OutlierPostProcessSceneViewExtension.h"
#include "FRDGAdsSightMaskPass.h"
#include "FRDGAdsSightRestorePass.h"
#include "FRDGSceneColorCopyPass.h"
#include "FRDGDualKawaseBlurPass.h"
#include "FRDGExplosionVolumePass.h"
#include "FRDGExplosionVolumeVisualizePass.h"
#include "FRDGHeatHazePass.h"
#include "FRDGMotionBlurPass.h"
#include "FRDGDatamoshingPass.h"
#include "FRDGPixelSortingPass.h"
#include "RDGExplosionVolumeProvider.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "FXRenderingUtils.h"
#include "PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphEvent.h"
#include "ScreenPass.h"
#include "SceneTexturesConfig.h"

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

	ENQUEUE_RENDER_COMMAND(DatamoshHistoryCleanup)(
		[this](FRHICommandListImmediate&)
		{
			constexpr uint64 StaleFrameThreshold = 120;
			const uint64 CurrentFrame = GFrameCounterRenderThread;
			for (auto It = DatamoshHistoryMap.CreateIterator(); It; ++It)
			{
				if (CurrentFrame > It.Value().LastTouchedFrame + StaleFrameThreshold)
				{
					It.RemoveCurrent();
				}
			}
		});
}

bool FOutlierPostProcessSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{

	return LocalPlayer.IsValid();
}

void FOutlierPostProcessSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (!ShouldRenderAnyEffect())
	{
		if (!FRDGExplosionVolumeVisualizePass::IsEnabled())
		{
			return;
		}
	}

	if (!IsTargetLocalPlayerView(View))
	{
		return;
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DualKawaseBlur.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DualKawaseBlurCallback_RenderThread));

		
	}
	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.Datamoshing.bEnabled)
	{

		//UE_LOG(LogTemp, Error, TEXT("CreateRaw"));

		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
			&FOutlierPostProcessSceneViewExtension::DatamoshingCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.PixelSorting.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::PixelSortingCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && FRDGExplosionVolumeVisualizePass::IsEnabled())
	{
		//UE_LOG(LogTemp, Error, TEXT("RDG.ExplosionVolume.Visualize"));

		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::ExplosionVolumeVisualizeCallback_RenderThread));
	}
	
	if (!bIsPassEnabled)
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

	if (PassId == EPostProcessingPass::BeforeDOF && CachedParameters.ADSBlur.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::ADSPreDoFCaptureCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::AfterDOF && CachedParameters.ADSBlur.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::ADSSightRestoreCallback_RenderThread));
	}
}

void FOutlierPostProcessSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	CachedADSSightHardMask = nullptr;
	CachedADSSightSoftMask = nullptr;
	CachedADSPreDoFSceneColor = nullptr;

	if (CachedParameters.ADSBlur.bEnabled && IsTargetLocalPlayerView(InView) && Inputs.SceneTextures)
	{
		const auto SceneTextureParameters = Inputs.SceneTextures->GetParameters();
		FRDGTextureRef CustomDepthTexture = SceneTextureParameters->CustomDepthTexture;
		FRDGTextureSRVRef CustomStencilTexture = SceneTextureParameters->CustomStencilTexture;
		if (CustomStencilTexture)
		{
			const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(InView);
			const FIntPoint MaskExtent = PrimaryViewRect.Size();
			const int32 UnscaledViewWidth = InView.UnscaledViewRect.Width();
			const float PrimaryResolutionFraction = UnscaledViewWidth > 0
				? static_cast<float>(MaskExtent.X) / static_cast<float>(UnscaledViewWidth)
				: 1.0f;
			const float ScaledDilateRadius = CachedParameters.ADSBlur.SightMaskDilateRadius * PrimaryResolutionFraction;
			const float ScaledSoftness = CachedParameters.ADSBlur.SightMaskSoftness * PrimaryResolutionFraction;

			FRDGTextureRef SightHardMask = FRDGAdsSightMaskPass::AddBuildMaskPass(
				GraphBuilder,
				InView,
				CustomStencilTexture,
				CustomDepthTexture,
				MaskExtent,
				CachedParameters.ADSBlur);

			CachedADSSightHardMask = FRDGAdsSightMaskPass::AddDilatePass(
				GraphBuilder,
				InView,
				SightHardMask,
				MaskExtent,
				ScaledDilateRadius);

			CachedADSSightSoftMask = FRDGAdsSightMaskPass::AddSoftenPass(
				GraphBuilder,
				InView,
				CachedADSSightHardMask,
				CachedADSSightHardMask,
				MaskExtent,
				ScaledSoftness);
		}
	}

	if (!FRDGExplosionVolumePass::IsEnabled() || !IsTargetLocalPlayerView(InView) || !Inputs.SceneTextures)
	{
		CachedVelocityVolume = nullptr;
		return;
	}

	FRDGTextureRef SceneDepthTexture = (*Inputs.SceneTextures)->SceneDepthTexture;
	if (!SceneDepthTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("SceneDepthTexture Invalid "));
		return;
	}



	FRDGTextureRef VelocityVolume = FRDGExplosionVolumePass::AddPass(
		GraphBuilder, InView, SceneDepthTexture);

	if (!VelocityVolume)
	{
		CachedVelocityVolume = nullptr;
		return;
	}

	FRDGExplosionVolumeProvider::QueueExtraction(GraphBuilder, VelocityVolume);
	CachedVelocityVolume = VelocityVolume;
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
		|| CachedParameters.Datamoshing.bEnabled
		|| CachedParameters.PixelSorting.bEnabled
		|| CachedParameters.ADSBlur.bEnabled
		|| HasHeatHazeSources();
}

bool FOutlierPostProcessSceneViewExtension::IsTargetLocalPlayerView(const FSceneView& InView) const
{
	if (!InView.Family || !InView.State)
	{
		return false;
	}

	ULocalPlayer* LP = LocalPlayer.Get();
	if (!LP)
	{
		return false;
	}


	const UWorld* LPWorld = LP->GetWorld();
	const FSceneInterface* Scene = InView.Family->Scene;
	return LPWorld != nullptr && Scene != nullptr && Scene->GetWorld() == LPWorld;
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
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGMotionBlurPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.MotionBlur,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DualKawaseBlurCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGDualKawaseBlurPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.DualKawaseBlur,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::ADSPreDoFCaptureCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	CachedADSPreDoFSceneColor = SceneColor.Texture;

	return SceneColor;
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::ADSSightRestoreCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	FRDGTextureRef SightMask = CachedParameters.ADSBlur.bUseSoftSightMask ? CachedADSSightSoftMask : CachedADSSightHardMask;
	if (!CachedADSPreDoFSceneColor || !SightMask)
	{
		return SceneColor;
	}

	const bool bEnableADSGpuStats = CachedParameters.ADSBlur.bEnableGpuStatScopes != 0;
	FRDGTextureRef Restored = FRDGAdsSightRestorePass::AddPass(
		GraphBuilder,
		SceneColor.Texture,
		CachedADSPreDoFSceneColor,
		SightMask,
		SceneColor.ViewRect,
		bEnableADSGpuStats);

	FScreenPassTexture RestoredTexture(Restored, SceneColor.ViewRect);

	if (Inputs.OverrideOutput.IsValid())
	{
		return FRDGSceneColorCopyPass::AddPass(
			GraphBuilder,
			View,
			RestoredTexture,
			Inputs.OverrideOutput);
	}

	return RestoredTexture;
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::HeatHazeCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

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
		HeatHazeSources,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DatamoshingCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("RENDERTHREAD CALL, !SceneColor.IsValid()"))

		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	FSceneViewState* ViewState = View.State ? View.State->GetConcreteViewState() : nullptr;	if (!ViewState)
	{
		UE_LOG(LogTemp, Error, TEXT("RENDERTHREAD CALL, !ViewState"))

		return SceneColor;
	}

	FDatamoshHistoryEntry& Entry = DatamoshHistoryMap.FindOrAdd(ViewState);
	Entry.LastTouchedFrame = GFrameCounterRenderThread;

	//UE_LOG(LogTemp, Error, TEXT("AddPass"))

	return FRDGDatamoshingPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.Datamoshing,
		Entry.RenderTarget,
		Inputs.OverrideOutput
	);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::PixelSortingCallback_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGPixelSortingPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.PixelSorting,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::ExplosionVolumeVisualizeCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGExplosionVolumeVisualizePass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedVelocityVolume,
		Inputs.OverrideOutput);
}
