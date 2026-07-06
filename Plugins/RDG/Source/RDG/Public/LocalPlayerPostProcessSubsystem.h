// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "FPostProcessStructures.h"
#include "Tickable.h"
#include "LocalPlayerPostProcessSubsystem.generated.h"

class FOutlierPostProcessSceneViewExtension;

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
	float GetADSBlurRampInTime() const { return ADSBlurRampInTime; }
	float GetADSBlurRampOutTime() const { return ADSBlurRampOutTime; }
	float GetADSBlurMinRadius() const { return ADSBlurMinRadius; }
	float GetADSBlurMaxRadius() const { return ADSBlurMaxRadius; }
	float GetADSBlurMinMaskDilate() const { return ADSBlurMinMaskDilate; }
	float GetADSBlurMaxMaskDilate() const { return ADSBlurMaxMaskDilate; }
	float GetADSBlurMinMaskSoftness() const { return ADSBlurMinMaskSoftness; }
	float GetADSBlurMaxMaskSoftness() const { return ADSBlurMaxMaskSoftness; }
	int32 GetADSBlurPassCount() const { return ADSBlurPassCount; }
	float GetADSBlurInnerPreserve() const { return ADSBlurInnerPreserve; }
	float GetADSBlurDepthBlurStart() const { return ADSBlurDepthBlurStart; }
	float GetADSBlurDepthBlurEnd() const { return ADSBlurDepthBlurEnd; }
	float GetADSBlurDepthBlurPower() const { return ADSBlurDepthBlurPower; }
	float GetADSBlurDepthFocusBias() const { return ADSBlurDepthFocusBias; }
	float GetADSSocketDistance() const { return ADSBlurSocketDistance; }
	int32 GetADSBlurGatherSampleCount() const { return ADSBlurGatherSampleCount; }
	float GetADSBlurReachSoftness() const { return ADSBlurReachSoftness; }
	void SetADSBlurPassCount(int32 InPassCount);
	void SetADSBlurInnerPreserve(float InInnerPreserve);
	void SetADSBlurDepthBlurRange(float InStart, float InEnd);
	void SetADSBlurDepthBlurPower(float InPower);
	void SetADSBlurDepthFocusBias(float InBias);
	void SetADSBlurGatherSampleCount(int32 InSampleCount);
	void SetADSBlurReachSoftness(float InReachSoftness);
	void SetADSBlurRampTimes(float InRampInTime, float InRampOutTime);
	void SetADSBlurRadiusRange(float InMinRadius, float InMaxRadius);
	void SetADSBlurMaskDilateRange(float InMinDilate, float InMaxDilate);
	void SetADSBlurMaskSoftnessRange(float InMinSoftness, float InMaxSoftness);
	void SetADSBlurMaskDilateRadius(float InDilateRadius);
	void SetADSBlurMaskSoftness(float InSoftness);

	void TickFrame();
	const FPostProcessStrcture& GetPostProcessStrcture();
	const FPostProcessStrcture& GetPostProcessStrcture() const;
	const FPostProcessStrctureUI& GetUIPostProcessStrcture() const;
	bool IsDirty();

private:
	void MarkDirty();
	void UpdateADSBlur(float DeltaTime);
	void ApplyADSBlurRuntimeParameters();
	float GetADSBlurAlpha() const;
	void SetADSBlurEnabled(bool bEnabled);
	void SetADSBlurBlend(float InAdsBlend);
	void SetADSBlurRadius(float InBlurRadius);
	void SetADSBlurMaskParameters(float InDilateRadius, float InSoftness);
	void SetADSBlurWeaponStencilValue(int32 InStencilValue);

	FPPGameplayState PlayerState;
	FPostProcessStrcture PostProcessParameters;
	FPostProcessStrcture CachedPostProcessParameters;

	FPostProcessStrctureUI CachedUIPostProcessParameters;
	FPostProcessStrctureUI UIPostProcessParameters;

	TSharedPtr<FOutlierPostProcessSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;

	uint8 bDirty : 1 = false;
	uint8 bADSBlurAiming : 1 = false;

	float ADSBlurElapsedTime = 0.0f;
	float ADSBlurRampInTime = 0.18f;
	float ADSBlurRampOutTime = 0.12f;

	float ADSBlurMinRadius = 0.0f;
	float ADSBlurMaxRadius = 4.0f;

	float ADSBlurMinMaskDilate = 1.0f;
	float ADSBlurMaxMaskDilate = 3.0f;

	float ADSBlurMinMaskSoftness = 0.5f;
	float ADSBlurMaxMaskSoftness = 5.0f;

	int32 ADSBlurPassCount = 2;
	float ADSBlurInnerPreserve = 0.f;

	float ADSBlurDepthBlurStart = 0.0f;
	float ADSBlurDepthBlurEnd = 0.8f;

	float ADSBlurDepthBlurPower = 1.2f;
	float ADSBlurDepthFocusBias = 0.1f;
	int32 ADSBlurGatherSampleCount = 48;
	float ADSBlurReachSoftness = 3.0f;

	float ADSBlurSocketDistance = 0.0f; 
};
