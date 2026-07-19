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

	void SetPixelSortingEnabled(bool bEnabled);
	void SetPixelSortingMode(int32 InMode);
	void SetPixelSortingCurve(int32 InCurve);
	void SetPixelSortingMinThreshold(int32 InMinThreshold);
	void SetPixelSortingScale(float InScale);
	void SetPixelSortingRowsEnabled(bool bEnabled);
	void SetPixelSortingColumnsEnabled(bool bEnabled);

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

	void SetADSBlurFocusDistanceWorld(float InFocusDistanceWorld);
	void SetADSBlurSightDistanceThreshold(float InThreshold);
	void SetADSBlurSightMaskDilateRadius(float InDilateRadius);
	void SetADSBlurSightMaskSoftness(float InSoftness);
	void SetADSBlurUseSoftSightMask(bool bInUseSoft);
	void SetADSBlurGpuStatScopesEnabled(bool bEnabled);

	void SetDepthOfFieldVolume(APostProcessVolume* InVolume);
	void SetADSDoFEnabled(bool bEnabled);
	void SetADSDoFApertureRange(float InAimFStop, float InHipFStop);
	void SetADSDoFSensorWidth(float InSensorWidth);
	void SetADSDoFMaxBlurClamp(float InMinFStop);
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
	void UpdatePixelSorting(float DeltaTime);
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

	float ADSBlurSocketDistance = 11.0f;

	TWeakObjectPtr<APostProcessVolume> DoFVolume;
	uint8 bADSDoFEnabled : 1 = true;
	float ADSDoFApertureAim = 14.47f;
	float ADSDoFApertureHip = 32.0f;
	float ADSDoFSensorWidth = 12.576f;
	float ADSDoFMinFStop = 0.0f;
	float ADSDoFFocalRegion = 0.0f;
	float ADSDoFFarTransitionRegion = 1500.0f;
};
