#include "OutlierPostProcessSceneViewExtension.h"
#include "FRDGDualKawaseBlurPass.h"
#include "FRDGExplosionVolumePass.h"
#include "FRDGExplosionVolumeVisualizePass.h"
#include "FRDGHeatHazePass.h"
#include "FRDGMotionBlurPass.h"
#include "FRDGDatamoshingPass.h"
#include "RDGExplosionVolumeProvider.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
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

	
	// 오래 안 쓴 ViewState 엔트리만 정리. 다중 뷰포트(빙의 + 에디터 등)가 ping-pong으로
	// 서로의 history를 지우지 않도록, 일정 프레임 이상 미사용일 때만 제거.

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

	if (PassId == EPostProcessingPass::Tonemap && FRDGExplosionVolumeVisualizePass::IsEnabled())
	{
		//UE_LOG(LogTemp, Error, TEXT("RDG.ExplosionVolume.Visualize"));


		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FOutlierPostProcessSceneViewExtension::ExplosionVolumeVisualizeCallback_RenderThread));
	}
	//else
	//{
	//	UE_LOG(LogTemp, Error, TEXT("RDG.ExplosionVolume.CantVisualize"));

	//}


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
}

void FOutlierPostProcessSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{

	UE_LOG(LogTemp, Warning, TEXT("PrePostProcess: IsTarget=%d, HasSceneTextures=%d"),
		IsTargetLocalPlayerView(InView) ? 1 : 0,
		Inputs.SceneTextures ? 1 : 0);


	if (!IsTargetLocalPlayerView(InView) || !Inputs.SceneTextures)
	{
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

	FRDGExplosionVolumeProvider::QueueExtraction(GraphBuilder, VelocityVolume);

	CachedVelocityVolume = VelocityVolume;
	//같은 프레임의 경우,
	//Excute 이후에 처리 되니깐최초 프레임은 못 읽더라도 그 다음부터는 읽게 할 수 있음.
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

	// LocalPlayer가 속한 World와 View가 그리는 World가 일치해야 대상 뷰로 판정.
	// (PIE LocalPlayer는 PIE World, 에디터 LocalPlayer는 에디터 World를 갖기 때문에
	//  빙의 중에도 둘이 자연스럽게 분리됨. SceneViewExtension은 LocalPlayer 단위로 생성됨.)

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

	// 1. ViewState 유효성 검사 및 키(Key) 추출
	FSceneViewState* ViewState = View.State ? View.State->GetConcreteViewState() : nullptr;	if (!ViewState)
	{
		UE_LOG(LogTemp, Error, TEXT("RENDERTHREAD CALL, !ViewState"))

		// 에디터의 특정 뷰포트나 씬 캡처 등 ViewState가 없는 경우 원본 반환
		return SceneColor;
	}

	// 2. 현재 뷰에 맵핑된 History 엔트리 획득 (없으면 새로 생성). 프레임 마킹으로 cleanup 방어.
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
