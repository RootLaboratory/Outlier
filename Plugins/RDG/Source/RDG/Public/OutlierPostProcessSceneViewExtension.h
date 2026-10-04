#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "FPostProcessStructures.h"
#include "RHIResources.h"
#include "HAL/CriticalSection.h"
#include "HeatHazeSourceComponent.h"
#include "RDGEffectSourceWorldSubsystem.h"

class ULocalPlayer;
class FSceneViewState;

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
	void UpdateCachedUIParameters(const FPostProcessStrctureUI& InParameters);
	void UpdateDeathTransitionTexture(EDeathTransitionTexture Slot, const FTextureRHIRef& InTexture);
	void UpdateDroneDamageMaskTexture(EDroneDamageMask Slot, const FTextureRHIRef& InTexture);
	void UpdateHeatHazeSources(const TArray<FHeatHazeSourceData>& InSources);
	void ResetRuntimeHistory_RenderThread();

private:
	bool ShouldRenderAnyEffect() const;
	bool IsTargetLocalPlayerView(const FSceneView& InView) const;
	bool HasHeatHazeSources() const;
	void CopyHeatHazeSources(TArray<FHeatHazeSourceData>& OutSources) const;

	FScreenPassTexture MotionBlurCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DualKawaseBlurCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture SplitPrismDefocusCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture ADSPreDoFCaptureCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture ADSSightRestoreCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture HeatHazeCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DatamoshingCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DroneDamageFeedbackCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DeathNoiseCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DeathFadeCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DeathBlackCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture DeathVignetteCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	FScreenPassTexture ExplosionVolumeVisualizeCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

private:
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	FPostProcessStrcture CachedParameters;
	FPostProcessStrctureUI CachedUIParameters;
	// EDeathTransitionTexture 순서.
	FTextureRHIRef DeathTransitionTextureRHIs[DeathTransitionTextureCount];
	// EDroneDamageMask 순서.
	FTextureRHIRef DroneDamageMaskTextureRHIs[DroneDamageMaskCount];

	mutable FCriticalSection HeatHazeSourcesCriticalSection;
	TArray<FHeatHazeSourceData> CachedHeatHazeSources;

	struct FDatamoshHistoryEntry
	{
		TRefCountPtr<IPooledRenderTarget> RenderTarget;
		uint64 LastTouchedFrame = 0;
	};
	TMap<FSceneViewState*, FDatamoshHistoryEntry> DatamoshHistoryMap;

	//
	FRDGTextureRef CachedVelocityVolume = nullptr;
	FRDGTextureRef CachedADSSightHardMask = nullptr;
	FRDGTextureRef CachedADSSightSoftMask = nullptr;

	FRDGTextureRef CachedADSPreDoFSceneColor = nullptr;
};
