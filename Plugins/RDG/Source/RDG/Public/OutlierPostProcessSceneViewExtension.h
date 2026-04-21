#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "FPostProcessStructures.h"

class ULocalPlayer;

class FOutlierPostProcessSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FOutlierPostProcessSceneViewExtension(const FAutoRegister& AutoRegister, ULocalPlayer* InLocalPlayer);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass PassId,
		const FSceneView& View,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

	virtual void PrePostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& InView,
		const FPostProcessingInputs& Inputs) override;


public:
	void UpdateCachedParameters(const FPostProcessStrcture& InParameters);

private:
	bool ShouldRenderAnyEffect() const;
	bool IsTargetLocalPlayerView(const FSceneView& InView) const;

	FScreenPassTexture MotionBlurCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

private:
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	FPostProcessStrcture CachedParameters;

};
