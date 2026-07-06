// Fill out your copyright notice in the Description page of Project Settings.

#include "LocalPlayerPostProcessSubsystem.h"

#include "OutlierPostProcessSceneViewExtension.h"
#include "RDGExplosionVolumeProvider.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"

void ULocalPlayerPostProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FOutlierPostProcessSceneViewExtension>(LP);
	}

	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::Deinitialize()
{
	Super::Deinitialize();
	ViewExtension.Reset();

	ENQUEUE_RENDER_COMMAND(ReleaseExplosionVolumeTexture)(
		[](FRHICommandListImmediate&)
		{
			FRDGExplosionVolumeProvider::Release_RenderThread();
		});
	FlushRenderingCommands();
}

void ULocalPlayerPostProcessSubsystem::Tick(float DeltaTime)
{
	UpdateADSBlur(DeltaTime);
}

TStatId ULocalPlayerPostProcessSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULocalPlayerPostProcessSubsystem, STATGROUP_Tickables);
}

bool ULocalPlayerPostProcessSubsystem::IsTickable() const
{
	return !IsTemplate() && GetLocalPlayer() != nullptr;
}

void ULocalPlayerPostProcessSubsystem::MarkDirty()
{
	CachedPostProcessParameters = PostProcessParameters;
	CachedUIPostProcessParameters = UIPostProcessParameters;
	bDirty = true;
}

void ULocalPlayerPostProcessSubsystem::ActivateSlideState()
{
	if (PlayerState.bIsSliding)
	{
		return;
	}

	PlayerState.bIsSliding = true;
	SetMotionBlurEnabled(true);
}

void ULocalPlayerPostProcessSubsystem::DeActivateSlideState()
{
	PlayerState.bIsSliding = false;
	SetMotionBlurEnabled(false);
}

void ULocalPlayerPostProcessSubsystem::SetMotionBlurEnabled(bool bEnabled)
{
	PostProcessParameters.MotionBlur.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetMotionBlurBlendWeight(float InBlendWeight)
{
	PostProcessParameters.MotionBlur.BlendWeight = FMath::Clamp(InBlendWeight, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetMotionBlurIntensity(float InIntensity)
{
	PostProcessParameters.MotionBlur.Intensity = FMath::Max(0.0f, InIntensity);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetMotionBlurVelocityScale(float InVelocityScale)
{
	PostProcessParameters.MotionBlur.VelocityScale = FMath::Max(0.0f, InVelocityScale);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::ActivateChromaticAberration()
{
	SetChromaticAberrationEnabled(true);
}

void ULocalPlayerPostProcessSubsystem::DeactivateChromaticAberration()
{
	SetChromaticAberrationEnabled(false);
}

void ULocalPlayerPostProcessSubsystem::SetChromaticAberrationEnabled(bool bEnabled)
{
	UIPostProcessParameters.ChromaticAberration.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
}

void ULocalPlayerPostProcessSubsystem::SetChromaticAberrationStartOffset(float InStartOffset)
{
	UIPostProcessParameters.ChromaticAberration.StartOffset = FMath::Clamp(InStartOffset, 0.0f, 1.0f);
	MarkDirty();
}

void ULocalPlayerPostProcessSubsystem::SetChromaticAberrationIntensity(float InIntensity)
{
	UIPostProcessParameters.ChromaticAberration.Intensity = FMath::Max(0.0f, InIntensity);
	MarkDirty();
}

void ULocalPlayerPostProcessSubsystem::SetDualKawaseBlurEnabled(bool bEnabled)
{
	PostProcessParameters.DualKawaseBlur.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();

}

void ULocalPlayerPostProcessSubsystem::SetDualKawaseBlurRadius(float InBlurRadius)
{
	PostProcessParameters.DualKawaseBlur.BlurRadius = FMath::Max(0.0f, InBlurRadius);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDualKawaseBlurBlendWeight(float InBlendWeight)
{
	PostProcessParameters.DualKawaseBlur.BlendWeight = FMath::Clamp(InBlendWeight, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();

}

void ULocalPlayerPostProcessSubsystem::SetDualKawaseBlurDownsampleCount(int32 InDownsampleCount)
{
	PostProcessParameters.DualKawaseBlur.DownsampleCount = FMath::Clamp(InDownsampleCount, 1, 6);
	MarkDirty();
	TickFrame();

}

void ULocalPlayerPostProcessSubsystem::SetDatamoshingEnabled(bool bEnabled)
{
	PostProcessParameters.Datamoshing.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDatamoshingProgress(float InProgress)
{
	PostProcessParameters.Datamoshing.Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurEnabled(bool bEnabled)
{
	PostProcessParameters.ADSBlur.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurBlend(float InAdsBlend)
{
	PostProcessParameters.ADSBlur.AdsBlend = FMath::Clamp(InAdsBlend, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurRadius(float InBlurRadius)
{
	PostProcessParameters.ADSBlur.BlurRadius = FMath::Max(0.0f, InBlurRadius);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurMaskParameters(float InDilateRadius, float InSoftness)
{
	PostProcessParameters.ADSBlur.MaskDilateRadius = FMath::Max(0.0f, InDilateRadius);
	PostProcessParameters.ADSBlur.MaskSoftness = FMath::Max(0.0f, InSoftness);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurWeaponStencilValue(int32 InStencilValue)
{
	PostProcessParameters.ADSBlur.WeaponStencilValue = FMath::Clamp(InStencilValue, 0, 255);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurAiming(bool bInAiming, int32 InWeaponStencilValue)
{
	SetADSBlurWeaponStencilValue(InWeaponStencilValue);
	bADSBlurAiming = bInAiming ? 1 : 0;
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSSocketDistance(float Distance)
{
	// Controller가 매 프레임 소켓(광학 조준점) - 카메라 거리를 넘겨줌. CoC 초점 거리 P.
	ADSBlurSocketDistance = FMath::Max(0.0f, Distance);
	PostProcessParameters.ADSBlur.FocusDistanceWorld = ADSBlurSocketDistance;

	PostProcessParameters.ADSBlur.FocusDistanceWorld = 11.0f;

	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurRampTimes(float InRampInTime, float InRampOutTime)
{
	ADSBlurRampInTime = FMath::Max(0.0f, InRampInTime);
	ADSBlurRampOutTime = FMath::Max(0.0f, InRampOutTime);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurRadiusRange(float InMinRadius, float InMaxRadius)
{
	ADSBlurMinRadius = FMath::Max(0.0f, InMinRadius);
	ADSBlurMaxRadius = FMath::Max(ADSBlurMinRadius, InMaxRadius);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurMaskDilateRange(float InMinDilate, float InMaxDilate)
{
	ADSBlurMinMaskDilate = FMath::Max(0.0f, InMinDilate);
	ADSBlurMaxMaskDilate = FMath::Max(ADSBlurMinMaskDilate, InMaxDilate);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurMaskSoftnessRange(float InMinSoftness, float InMaxSoftness)
{
	ADSBlurMinMaskSoftness = FMath::Max(0.0f, InMinSoftness);
	ADSBlurMaxMaskSoftness = FMath::Max(ADSBlurMinMaskSoftness, InMaxSoftness);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurMaskDilateRadius(float InDilateRadius)
{
	SetADSBlurMaskDilateRange(InDilateRadius, InDilateRadius);
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurMaskSoftness(float InSoftness)
{
	SetADSBlurMaskSoftnessRange(InSoftness, InSoftness);
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurPassCount(int32 InPassCount)
{
	ADSBlurPassCount = FMath::Clamp(InPassCount, 1, 4);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurInnerPreserve(float InInnerPreserve)
{
	ADSBlurInnerPreserve = FMath::Clamp(InInnerPreserve, 0.0f, 1.0f);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurDepthBlurRange(float InStart, float InEnd)
{
	ADSBlurDepthBlurStart = FMath::Max(0.0f, InStart);
	ADSBlurDepthBlurEnd = FMath::Max(ADSBlurDepthBlurStart + KINDA_SMALL_NUMBER, InEnd);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurDepthBlurPower(float InPower)
{
	ADSBlurDepthBlurPower = FMath::Max(KINDA_SMALL_NUMBER, InPower);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurDepthFocusBias(float InBias)
{
	ADSBlurDepthFocusBias = InBias;
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurGatherSampleCount(int32 InSampleCount)
{
	ADSBlurGatherSampleCount = FMath::Clamp(InSampleCount, 1, 96);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurReachSoftness(float InReachSoftness)
{
	ADSBlurReachSoftness = FMath::Max(0.0f, InReachSoftness);
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::UpdateADSBlur(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (!bADSBlurAiming && ADSBlurElapsedTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float RampInTime = FMath::Max(ADSBlurRampInTime, KINDA_SMALL_NUMBER);
	const float RampOutTime = FMath::Max(ADSBlurRampOutTime, KINDA_SMALL_NUMBER);

	if (bADSBlurAiming)
	{
		ADSBlurElapsedTime = FMath::Min(ADSBlurElapsedTime + DeltaTime, RampInTime);
	}
	else
	{
		const float CurrentAlpha = GetADSBlurAlpha();
		ADSBlurElapsedTime = FMath::Max((CurrentAlpha - DeltaTime / RampOutTime) * RampInTime, 0.0f);
	}

	ApplyADSBlurRuntimeParameters();
}

float ULocalPlayerPostProcessSubsystem::GetADSBlurAlpha() const
{
	const float RampInTime = FMath::Max(ADSBlurRampInTime, KINDA_SMALL_NUMBER);
	const float LinearAlpha = FMath::Clamp(ADSBlurElapsedTime / RampInTime, 0.0f, 1.0f);
	return FMath::InterpEaseInOut(0.0f, 1.0f, LinearAlpha, 2.0f);
}

void ULocalPlayerPostProcessSubsystem::ApplyADSBlurRuntimeParameters()
{
	const float Alpha = GetADSBlurAlpha();

	PostProcessParameters.ADSBlur.bEnabled = Alpha > KINDA_SMALL_NUMBER ? 1 : 0;
	PostProcessParameters.ADSBlur.AdsBlend = Alpha;
	PostProcessParameters.ADSBlur.BlurRadius = FMath::Lerp(ADSBlurMinRadius, ADSBlurMaxRadius, Alpha);
	PostProcessParameters.ADSBlur.PassCount = ADSBlurPassCount;
	PostProcessParameters.ADSBlur.MaskDilateRadius = FMath::Lerp(ADSBlurMinMaskDilate, ADSBlurMaxMaskDilate, Alpha);
	PostProcessParameters.ADSBlur.MaskSoftness = FMath::Lerp(ADSBlurMinMaskSoftness, ADSBlurMaxMaskSoftness, Alpha);
	PostProcessParameters.ADSBlur.InnerPreserve = ADSBlurInnerPreserve;
	PostProcessParameters.ADSBlur.DepthBlurStart = ADSBlurDepthBlurStart;
	PostProcessParameters.ADSBlur.DepthBlurEnd = ADSBlurDepthBlurEnd;
	PostProcessParameters.ADSBlur.DepthBlurPower = ADSBlurDepthBlurPower;
	PostProcessParameters.ADSBlur.DepthFocusBias = ADSBlurDepthFocusBias;
	PostProcessParameters.ADSBlur.FocusDistanceWorld = ADSBlurSocketDistance;
	PostProcessParameters.ADSBlur.GatherSampleCount = ADSBlurGatherSampleCount;
	PostProcessParameters.ADSBlur.ReachSoftness = ADSBlurReachSoftness;

	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::TickFrame()
{
	if (!bDirty)
	{
		return;
	}

	if (ViewExtension.IsValid())
	{
		ViewExtension->UpdateCachedParameters(CachedPostProcessParameters);
		ViewExtension->UpdateCachedUIParameters(CachedUIPostProcessParameters);
	}

	bDirty = false;
}

const FPostProcessStrcture& ULocalPlayerPostProcessSubsystem::GetPostProcessStrcture()
{
	return CachedPostProcessParameters;
}

const FPostProcessStrcture& ULocalPlayerPostProcessSubsystem::GetPostProcessStrcture() const
{
	return CachedPostProcessParameters;
}

const FPostProcessStrctureUI& ULocalPlayerPostProcessSubsystem::GetUIPostProcessStrcture() const
{
	return CachedUIPostProcessParameters;
}

bool ULocalPlayerPostProcessSubsystem::IsDirty()
{
	return bDirty;
}
