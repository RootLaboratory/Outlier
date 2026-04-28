#include "OutlierPostProcessSceneViewExtension.h"

#include "FRDGDualKawaseBlurPass.h"
#include "FRDGMotionBlurPass.h"
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
}

void FOutlierPostProcessSceneViewExtension::UpdateCachedUIParameters(const FPostProcessStrctureUI& InParameters)
{
	CachedUIParameters = InParameters;
}

void FOutlierPostProcessSceneViewExtension::UpdateCachedParameters(const FPostProcessStrcture& InParameters)
{
	CachedParameters = InParameters;
}

bool FOutlierPostProcessSceneViewExtension::ShouldRenderAnyEffect() const
{
	return CachedParameters.MotionBlur.bEnabled
		|| CachedParameters.LensFlare.bEnabled
		|| CachedParameters.BloomBlur.bEnabled
		|| CachedParameters.DualKawaseBlur.bEnabled;
}

bool FOutlierPostProcessSceneViewExtension::IsTargetLocalPlayerView(const FSceneView& InView) const
{

	if (!InView.Family)
	{
		return false;
	}

	return LocalPlayer.IsValid();
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
