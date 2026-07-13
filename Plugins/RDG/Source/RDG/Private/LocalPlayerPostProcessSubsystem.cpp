#include "LocalPlayerPostProcessSubsystem.h"

#include "OutlierPostProcessSceneViewExtension.h"
#include "RDGExplosionVolumeProvider.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "Engine/PostProcessVolume.h"

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
	UpdateDepthOfField();
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

void ULocalPlayerPostProcessSubsystem::SetADSBlurWeaponStencilValue(int32 InStencilValue)
{
	PostProcessParameters.ADSBlur.WeaponStencilValue = FMath::Clamp(InStencilValue, 0, 255);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurFocusDistanceWorld(float InFocusDistanceWorld)
{
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurSightDistanceThreshold(float InThreshold)
{
	PostProcessParameters.ADSBlur.SightDistanceThreshold = FMath::Max(0.0f, InThreshold);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurSightMaskDilateRadius(float InDilateRadius)
{
	PostProcessParameters.ADSBlur.SightMaskDilateRadius = FMath::Max(0.0f, InDilateRadius);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurSightMaskSoftness(float InSoftness)
{
	PostProcessParameters.ADSBlur.SightMaskSoftness = FMath::Max(0.0f, InSoftness);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurUseSoftSightMask(bool bInUseSoft)
{
	PostProcessParameters.ADSBlur.bUseSoftSightMask = bInUseSoft ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurGpuStatScopesEnabled(bool bEnabled)
{
	PostProcessParameters.ADSBlur.bEnableGpuStatScopes = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurAiming(bool bInAiming, int32 InWeaponStencilValue)
{
	SetADSBlurWeaponStencilValue(InWeaponStencilValue);
	bADSBlurAiming = bInAiming ? 1 : 0;
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurDebugPassEnabled(bool bEnabled)
{
	bADSBlurDebugPassEnabled = bEnabled ? 1 : 0;
	ApplyADSBlurRuntimeParameters();
}

void ULocalPlayerPostProcessSubsystem::SetADSSocketDistance(float Distance)
{
	//ADSBlurSocketDistance = FMath::Max(0.0f, Distance);

	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDepthOfFieldVolume(APostProcessVolume* InVolume)
{
	DoFVolume = InVolume;
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFEnabled(bool bEnabled)
{
	bADSDoFEnabled = bEnabled ? 1 : 0;
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFApertureRange(float InAimFStop, float InHipFStop)
{
	ADSDoFApertureAim = FMath::Max(0.1f, InAimFStop);
	ADSDoFApertureHip = FMath::Max(0.1f, InHipFStop);
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFSensorWidth(float InSensorWidth)
{
	ADSDoFSensorWidth = FMath::Max(1.0f, InSensorWidth);
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFMaxBlurClamp(float InMinFStop)
{
	ADSDoFMinFStop = FMath::Max(0.0f, InMinFStop);
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFFocalRegion(float InFocalRegion)
{
	ADSDoFFocalRegion = FMath::Max(0.0f, InFocalRegion);
}

void ULocalPlayerPostProcessSubsystem::SetADSDoFFarTransitionRegion(float InFarTransitionRegion)
{
	ADSDoFFarTransitionRegion = FMath::Max(0.0f, InFarTransitionRegion);
}

void ULocalPlayerPostProcessSubsystem::UpdateDepthOfField()
{
	APostProcessVolume* Volume = DoFVolume.Get();
	if (!Volume)
	{
		return;
	}

	FPostProcessSettings& PP = Volume->Settings;
	const float Alpha = GetADSBlurAlpha();

	if (!bADSDoFEnabled || Alpha <= KINDA_SMALL_NUMBER || ADSBlurSocketDistance <= 0.0f)
	{
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.DepthOfFieldFocalDistance = 0.0f;
		return;
	}

	const float FStop = FMath::Lerp(ADSDoFApertureHip, ADSDoFApertureAim, Alpha);

	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = ADSBlurSocketDistance;

	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = FStop;

	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = ADSDoFSensorWidth;

	PP.bOverride_DepthOfFieldMinFstop = true;
	PP.DepthOfFieldMinFstop = ADSDoFMinFStop;

	PP.bOverride_DepthOfFieldFocalRegion = true;
	PP.DepthOfFieldFocalRegion = ADSDoFFocalRegion;

	PP.bOverride_DepthOfFieldFarTransitionRegion = true;
	PP.DepthOfFieldFarTransitionRegion = ADSDoFFarTransitionRegion;
}

void ULocalPlayerPostProcessSubsystem::SetADSBlurRampTimes(float InRampInTime, float InRampOutTime)
{
	ADSBlurRampInTime = FMath::Max(0.0f, InRampInTime);
	ADSBlurRampOutTime = FMath::Max(0.0f, InRampOutTime);
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
		const float DecrementInElapsed = DeltaTime * (RampInTime / RampOutTime);
		ADSBlurElapsedTime = FMath::Max(ADSBlurElapsedTime - DecrementInElapsed, 0.0f);
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

	PostProcessParameters.ADSBlur.bEnabled = bADSBlurDebugPassEnabled && Alpha > KINDA_SMALL_NUMBER ? 1 : 0;
	PostProcessParameters.ADSBlur.FocusDistanceWorld = 10.5f;

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
