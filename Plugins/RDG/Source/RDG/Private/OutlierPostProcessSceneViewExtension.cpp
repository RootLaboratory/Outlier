#include "OutlierPostProcessSceneViewExtension.h"


#include "FRDGMotionBlurPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"

FOutlierPostProcessSceneViewExtension::FOutlierPostProcessSceneViewExtension(const FAutoRegister& AutoRegister, ULocalPlayer* InLocalPlayer) : FSceneViewExtensionBase(AutoRegister) , LocalPlayer(InLocalPlayer)
{
	//UE_LOG(LogTemp, Error, TEXT("Initalized"));
}

void FOutlierPostProcessSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{

}

void FOutlierPostProcessSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{

}

//Called on game thread when view family is about to be rendered.

void FOutlierPostProcessSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{

	//UE_LOG(LogTemp, Error, TEXT("BeginRenderViewFamily"));

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

		if (CachedParameters.MotionBlur.bEnabled)
		{
			// TODO: MotionBlur RDG pass enqueue
		}

		if (CachedParameters.LensFlare.bEnabled)
		{
			// TODO: LensFlare RDG pass enqueue
		}

		if (CachedParameters.BloomBlur.bEnabled)
		{
			// TODO: BloomBlur RDG pass enqueue
		}
	}


}

bool FOutlierPostProcessSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{

	UWorld* ContextWorld = Context.GetWorld();
	UWorld* LocalPlayerWorld = nullptr;

	if (LocalPlayer.IsValid())
	{
		LocalPlayerWorld = LocalPlayer->GetWorld();
	}

	/*UE_LOG(LogTemp, Warning,
		TEXT("IsActiveThisFrame_Internal | LP=%d Viewport=%p ContextWorld=%s LPWorld=%s SameWorld=%d"),
		LocalPlayer.IsValid() ? 1 : 0,
		Context.Viewport,
		ContextWorld ? *ContextWorld->GetName() : TEXT("None"),
		LocalPlayerWorld ? *LocalPlayerWorld->GetName() : TEXT("None"),
		(ContextWorld && LocalPlayerWorld && ContextWorld == LocalPlayerWorld) ? 1 : 0);*/

	return LocalPlayer.IsValid();// && ShouldRenderAnyEffect();
}

void FOutlierPostProcessSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{

	/*UE_LOG(LogTemp, Warning,
		TEXT("PP PassId=%d Enabled=%d CachedMB=%d"),
		(int32)PassId,
		bIsPassEnabled ? 1 : 0,
		CachedParameters.MotionBlur.bEnabled ? 1 : 0);*/
	if (!bIsPassEnabled)
	{
		return;
	}

	if (!CachedParameters.MotionBlur.bEnabled)
	{
		return;
	}

	if (!IsTargetLocalPlayerView(View))
	{
		return;
	}

	if (PassId != EPostProcessingPass::MotionBlur)
	{
	//	UE_LOG(LogTemp, Error, TEXT("EPostProcessingPass::MotionBlur"));

		return;
	}

	InOutPassCallbacks.Add(
		FAfterPassCallbackDelegate::CreateRaw(
			this,
			&FOutlierPostProcessSceneViewExtension::MotionBlurCallback_RenderThread));
}

void FOutlierPostProcessSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
//	UE_LOG(LogTemp, Warning, TEXT("PrePostProcessPass_RenderThread"));

}

void FOutlierPostProcessSceneViewExtension::UpdateCachedParameters(const FPostProcessStrcture& InParameters)
{
	CachedParameters = InParameters;

}

bool FOutlierPostProcessSceneViewExtension::ShouldRenderAnyEffect() const
{
	//UE_LOG(LogTemp, Error, TEXT("ShouldRenderAnyEffect"));
	return
		CachedParameters.MotionBlur.bEnabled
		|| CachedParameters.LensFlare.bEnabled
		|| CachedParameters.BloomBlur.bEnabled;
}

bool FOutlierPostProcessSceneViewExtension::IsTargetLocalPlayerView(const FSceneView& InView) const
{
	return LocalPlayer.IsValid() ;
}

FScreenPassTexture FOutlierPostProcessSceneViewExtension::MotionBlurCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor),
		Inputs.OverrideOutput);

	if (!SceneColor.IsValid())
	{
		//UE_LOG(LogTemp, Error, TEXT("ReturnUntouchedSceneColorForPostProcessing"));

		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}


	/*UE_LOG(LogTemp, Warning, TEXT("MotionBlur Params | bEnabled=%d BlendWeight=%.3f Intensity=%.3f VelocityScale=%.3f"),
		CachedParameters.MotionBlur.bEnabled ? 1 : 0,
		CachedParameters.MotionBlur.BlendWeight,
		CachedParameters.MotionBlur.Intensity,
		CachedParameters.MotionBlur.VelocityScale);*/


	return FRDGMotionBlurPass::AddPass(
		GraphBuilder,
		View,
		SceneColor,
		CachedParameters.MotionBlur);
}
