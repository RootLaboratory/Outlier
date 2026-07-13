// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "FPostProcessStructures.h"
#include "Tickable.h"
#include "LocalPlayerPostProcessSubsystem.generated.h"

class FOutlierPostProcessSceneViewExtension;
class APostProcessVolume;

struct FPPGameplayState
{
	uint8 bIsSliding : 1 = false;
};

UCLASS()
class RDG_API ULocalPlayerPostProcessSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	void ActivateSlideState();
	void DeActivateSlideState();
	void SetMotionBlurEnabled(bool bEnabled);
	void SetMotionBlurBlendWeight(float InBlendWeight);
	void SetMotionBlurIntensity(float InIntensity);
	void SetMotionBlurVelocityScale(float InVelocityScale);

	void ActivateChromaticAberration();
	void DeactivateChromaticAberration();
	void SetChromaticAberrationEnabled(bool bEnabled);
	void SetChromaticAberrationStartOffset(float InStartOffset);
	void SetChromaticAberrationIntensity(float InIntensity);

	void SetDualKawaseBlurEnabled(bool bEnabled);
	void SetDualKawaseBlurRadius(float InBlurRadius);
	void SetDualKawaseBlurBlendWeight(float InBlendWeight);
	void SetDualKawaseBlurDownsampleCount(int32 InDownsampleCount);

	void SetDatamoshingEnabled(bool bEnabled);
	void SetDatamoshingProgress(float InProgress);

	UFUNCTION(BlueprintCallable, Category = "RDG|ADS Blur")
	void SetADSBlurAiming(bool bInAiming, int32 InWeaponStencilValue = 3);
	void SetADSSocketDistance(float Distance);
	bool IsADSBlurAiming() const { return bADSBlurAiming; }
	bool IsADSBlurDebugPassEnabled() const { return bADSBlurDebugPassEnabled; }
	void SetADSBlurDebugPassEnabled(bool bEnabled);
	float GetADSBlurRampInTime() const { return ADSBlurRampInTime; }
	float GetADSBlurRampOutTime() const { return ADSBlurRampOutTime; }
	float GetADSSocketDistance() const { return ADSBlurSocketDistance; }
	void SetADSBlurRampTimes(float InRampInTime, float InRampOutTime);

	// Sight-tube mask tuning (see FRDGAdsSightMaskPass / FRDGAdsSightRestorePass).
	void SetADSBlurSightDistanceThreshold(float InThreshold);
	void SetADSBlurSightMaskDilateRadius(float InDilateRadius);
	void SetADSBlurSightMaskSoftness(float InSoftness);
	void SetADSBlurUseSoftSightMask(bool bInUseSoft);
	void SetADSBlurGpuStatScopesEnabled(bool bEnabled);

	// --- ADS Depth of Field (UE Diaphragm DOF, written to an unbound APostProcessVolume) ---
	// The game registers the level's post-process volume here (engine base type, so the plugin
	// stays free of any game-module dependency). Each tick the aim ramp alpha + socket focus
	// distance drive the volume's DoF settings; the debugger tunes the parameters below live.
	void SetDepthOfFieldVolume(APostProcessVolume* InVolume);
	void SetADSDoFEnabled(bool bEnabled);
	void SetADSDoFApertureRange(float InAimFStop, float InHipFStop);
	void SetADSDoFSensorWidth(float InSensorWidth);
	// Caps how far the aperture can open regardless of Intensity/Aim f-stop — a ceiling on
	// maximum blur strength (UE's DepthOfFieldMinFstop; naming is inverted from what you'd
	// expect: it's a *minimum f-stop value*, which is the *widest* aperture allowed).
	void SetADSDoFMaxBlurClamp(float InMinFStop);
	// Distance (cm) around the focal point that stays fully sharp, and the distance beyond that
	// over which background blur ramps up to its max — i.e. how far from focus something has to
	// be before it hits max CoC (UE's DepthOfFieldFocalRegion / DepthOfFieldFarTransitionRegion).
	void SetADSDoFFocalRegion(float InFocalRegion);
	void SetADSDoFFarTransitionRegion(float InFarTransitionRegion);
	bool IsADSDoFEnabled() const { return bADSDoFEnabled; }
	float GetADSDoFApertureAim() const { return ADSDoFApertureAim; }
	float GetADSDoFApertureHip() const { return ADSDoFApertureHip; }
	float GetADSDoFSensorWidth() const { return ADSDoFSensorWidth; }
	float GetADSDoFMaxBlurClamp() const { return ADSDoFMinFStop; }
	float GetADSDoFFocalRegion() const { return ADSDoFFocalRegion; }
	float GetADSDoFFarTransitionRegion() const { return ADSDoFFarTransitionRegion; }

	void TickFrame();
	const FPostProcessStrcture& GetPostProcessStrcture();
	const FPostProcessStrcture& GetPostProcessStrcture() const;
	const FPostProcessStrctureUI& GetUIPostProcessStrcture() const;
	bool IsDirty();

private:
	void MarkDirty();
	void UpdateADSBlur(float DeltaTime);
	void UpdateDepthOfField();
	void ApplyADSBlurRuntimeParameters();
	float GetADSBlurAlpha() const;
	void SetADSBlurWeaponStencilValue(int32 InStencilValue);

	FPPGameplayState PlayerState;
	FPostProcessStrcture PostProcessParameters;
	FPostProcessStrcture CachedPostProcessParameters;

	FPostProcessStrctureUI CachedUIPostProcessParameters;
	FPostProcessStrctureUI UIPostProcessParameters;

	TSharedPtr<FOutlierPostProcessSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;

	uint8 bDirty : 1 = false;
	uint8 bADSBlurAiming : 1 = false;
	uint8 bADSBlurDebugPassEnabled : 1 = true;

	float ADSBlurElapsedTime = 0.0f;
	float ADSBlurRampInTime = 0.18f;
	float ADSBlurRampOutTime = 0.12f;

	float ADSBlurSocketDistance = 0.0f;

	// ADS Depth of Field (UE Diaphragm DOF driven on the registered volume).
	TWeakObjectPtr<APostProcessVolume> DoFVolume;
	uint8 bADSDoFEnabled : 1 = true;
	// f-stop at full ADS (lower = shallower = more blur). 14.47 corresponds to the debugger's
	// Intensity slider default of 0.1 (see kADSDoFIntensityMinFStop/MaxFStop in
	// SRDGGraphicsDebugger.cpp: FStop = Lerp(16.0, 0.7, Intensity)).
	float ADSDoFApertureAim = 14.47f;
	float ADSDoFApertureHip = 32.0f;   // f-stop at hip; DoF fully off at alpha 0 regardless
	float ADSDoFSensorWidth = 12.576f; // mm; larger = stronger blur for a given f-stop
	float ADSDoFMinFStop = 0.0f;       // UE DepthOfFieldMinFstop; 0 = no cap on max blur
	float ADSDoFFocalRegion = 0.0f;           // UE DepthOfFieldFocalRegion; cm fully in focus around FocalDistance
	float ADSDoFFarTransitionRegion = 1500.0f; // UE DepthOfFieldFarTransitionRegion; cm past FocalRegion until CoC saturates
};
