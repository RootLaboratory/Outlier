#include "OutlierPostProcessSceneViewExtension.h"
#include "FRDGAdsSightMaskPass.h"
#include "FRDGAdsSightRestorePass.h"
#include "FRDGSceneColorCopyPass.h"
#include "FRDGDualKawaseBlurPass.h"
#include "FRDGExplosionVolumePass.h"
#include "FRDGExplosionVolumeVisualizePass.h"
#include "FRDGHeatHazePass.h"
#include "FRDGMotionBlurPass.h"
#include "FRDGSplitPrismDefocusPass.h"
#include "FRDGDatamoshingPass.h"
#include "FRDGDeathBlackPass.h"
#include "FRDGDeathColorLerpPass.h"
#include "FRDGDeathNoisePass.h"
#include "FRDGDroneDamageFeedbackPass.h"
#include "RDGExplosionVolumeProvider.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "FXRenderingUtils.h"
#include "PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphEvent.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
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

void FOutlierPostProcessSceneViewExtension::ResetRuntimeHistory_RenderThread()
{
	check(IsInRenderingThread());
	DatamoshHistoryMap.Reset();
	CachedVelocityVolume = nullptr;
	CachedADSSightHardMask = nullptr;
	CachedADSSightSoftMask = nullptr;
	CachedADSPreDoFSceneColor = nullptr;
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

	// 드론 피격 마스크 글리치. Tonemap 이후라 UI는 안 먹는다. 사망 연출 중엔 HUD와 함께 꺼진다.
	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DroneDamageFeedback.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DroneDamageFeedbackCallback_RenderThread));
	}

	// 사망 연출: Fade → Black → Vignette → Noise. Tonemap 이후라 UI는 안 먹는다.
	// 순서가 곧 합성 순서라서 Vignette / Noise가 Fade/Black 결과 위에 얹혀 Black 단계까지 계속 보인다.
	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DeathFade.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DeathFadeCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DeathBlack.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DeathBlackCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DeathFade.bVignetteEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DeathVignetteCallback_RenderThread));
	}

	if (PassId == EPostProcessingPass::Tonemap && CachedParameters.DeathNoise.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::DeathNoiseCallback_RenderThread));
	}

	// Pixel Sorting은 여기서 돌지 않음. Slate 이후 backbuffer 단계(FRDGModule::HandleBackBufferReadyRDG)로
	// 옮겨서 UI까지 포함한 최종 화면에 적용됨.

	if (PassId == EPostProcessingPass::Tonemap && FRDGExplosionVolumeVisualizePass::IsEnabled())
	{
		//UE_LOG(LogTemp, Error, TEXT("RDG.ExplosionVolume.Visualize"));

		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::ExplosionVolumeVisualizeCallback_RenderThread));
	}

	// Split Prism 디포커스. Tonemap 앞 선형 HDR이라 밝은 점이 빛망울로 퍼지고, 블룸도 흐려진 결과에서 다시 만들어진다.
	// 엔진은 모션 블러가 꺼져 있어도 이 자리 콜백을 실행하므로 아래 bIsPassEnabled 검사보다 앞에 둔다.
	// 갈라짐은 Slate 이후 backbuffer 단계(FRDGModule::HandleBackBufferReadyRDG)에서 한다.
	if (PassId == EPostProcessingPass::MotionBlur && CachedParameters.SplitPrismDefocus.bEnabled)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::SplitPrismDefocusCallback_RenderThread));
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

void FOutlierPostProcessSceneViewExtension::UpdateDeathTransitionTexture(EDeathTransitionTexture Slot, const FTextureRHIRef& InTexture)
{
	if (Slot < EDeathTransitionTexture::Count)
	{
		DeathTransitionTextureRHIs[static_cast<int32>(Slot)] = InTexture;
	}
}

void FOutlierPostProcessSceneViewExtension::UpdateDroneDamageMaskTexture(EDroneDamageMask Slot, const FTextureRHIRef& InTexture)
{
	if (Slot < EDroneDamageMask::Count)
	{
		DroneDamageMaskTextureRHIs[static_cast<int32>(Slot)] = InTexture;
	}
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
		|| CachedParameters.SplitPrismDefocus.bEnabled
		|| CachedParameters.DroneDamageFeedback.bEnabled
		|| CachedParameters.Datamoshing.bEnabled
		|| CachedParameters.ADSBlur.bEnabled
		|| CachedParameters.DeathNoise.bEnabled
		|| CachedParameters.DeathFade.bEnabled
		|| CachedParameters.DeathFade.bVignetteEnabled
		|| CachedParameters.DeathBlack.bEnabled
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

FScreenPassTexture FOutlierPostProcessSceneViewExtension::SplitPrismDefocusCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// 깊이 잔차용. 없으면 패스가 잔차를 끄고 블러만 한다.
	FRDGTextureRef SceneDepthTexture = Inputs.SceneTextures.SceneTextures
		? Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture
		: nullptr;

	return FRDGSplitPrismDefocusPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		SceneDepthTexture,
		CachedParameters.SplitPrismDefocus,
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

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DroneDamageFeedbackCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	TStaticArray<FRHITexture*, DroneDamageMaskCount> MaskTextures;
	for (int32 Index = 0; Index < DroneDamageMaskCount; ++Index)
	{
		MaskTextures[Index] = DroneDamageMaskTextureRHIs[Index].GetReference();
	}

	return FRDGDroneDamageFeedbackPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.DroneDamageFeedback,
		MaskTextures,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DeathNoiseCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGDeathNoisePass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.DeathNoise,
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DeathFadeCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// 비네트는 Black 다음에 따로 그린다. 여기서 더하면 Black에 덮인다.
	const FDeathFadeParameters& Fade = CachedParameters.DeathFade;
	const float Amount = Fade.bEnabled ? GetDeathFadeAmount(Fade) : 0.0f;

	return FRDGDeathColorLerpPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		Fade.TargetColor,
		Amount,
		nullptr,
		0.0f,
		TEXT("RDG.DeathFade"),
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DeathBlackCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	return FRDGDeathBlackPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.DeathBlack,
		DeathTransitionTextureRHIs[static_cast<int32>(EDeathTransitionTexture::BlackBackground)].GetReference(),
		DeathTransitionTextureRHIs[static_cast<int32>(EDeathTransitionTexture::BlackNoise)].GetReference(),
		Inputs.OverrideOutput);
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::DeathVignetteCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// Fade 셰이더를 보간량 0으로 돌려서 비네트만 더한다.
	const FDeathFadeParameters& Fade = CachedParameters.DeathFade;
	const float VignetteAmount = Fade.bVignetteEnabled ? GetDeathFadeVignetteAmount(Fade) : 0.0f;

	return FRDGDeathColorLerpPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		FLinearColor::Black,
		0.0f,
		DeathTransitionTextureRHIs[static_cast<int32>(EDeathTransitionTexture::FadeVignette)].GetReference(),
		VignetteAmount,
		TEXT("RDG.DeathVignette"),
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
