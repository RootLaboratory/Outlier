#include "LocalPlayerPostProcessSubsystem.h"

#include "OutlierPostProcessSceneViewExtension.h"
#include "RDGExplosionVolumeProvider.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "Engine/PostProcessVolume.h"

namespace PostProcessAnimation
{
	float ApplyCurve(float Progress, int32 Curve)
	{
		if (Curve == static_cast<int32>(EPixelSortingCurve::CubicEaseIn))
		{
			return Progress * Progress * Progress;
		}

		return Progress;
	}
}

namespace PixelSortingAnimation
{
	void Reset(FPixelSortingParameters& Parameters)
	{
		Parameters.Threshold = 255.0f;
		Parameters.Progress = 0.0f;
	}
}

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
	OnHackTransitionCovered.Clear();
	OnHackTransitionFinished.Clear();
	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Idle;
	bHackTransitionCoveredBroadcastSent = false;

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
	UpdatePixelSorting(DeltaTime);
	UpdateHackPossessionTransition(DeltaTime);
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

void ULocalPlayerPostProcessSubsystem::SetPixelSortingEnabled(bool bEnabled)
{
	const bool bWasEnabled = PostProcessParameters.PixelSorting.bEnabled != 0;
	PostProcessParameters.PixelSorting.bEnabled = bEnabled ? 1 : 0;
	if (!bEnabled || !bWasEnabled)
	{
		PixelSortingAnimation::Reset(PostProcessParameters.PixelSorting);
	}
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingMode(int32 InMode)
{
	PostProcessParameters.PixelSorting.Mode = FMath::Clamp(
		InMode,
		static_cast<int32>(EPixelSortingMode::White),
		static_cast<int32>(EPixelSortingMode::Dark));
	PixelSortingAnimation::Reset(PostProcessParameters.PixelSorting);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingCurve(int32 InCurve)
{
	FPixelSortingParameters& Parameters = PostProcessParameters.PixelSorting;
	Parameters.Curve = FMath::Clamp(
		InCurve,
		static_cast<int32>(EPixelSortingCurve::Linear),
		static_cast<int32>(EPixelSortingCurve::CubicEaseIn));
	PixelSortingAnimation::Reset(Parameters);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingMinThreshold(int32 InMinThreshold)
{
	FPixelSortingParameters& Parameters = PostProcessParameters.PixelSorting;
	Parameters.MinThreshold = FMath::Clamp(InMinThreshold, 0, 255);
	PostProcessParameters.ZoomBlur.TriggerThreshold = FMath::Clamp(
		PostProcessParameters.ZoomBlur.TriggerThreshold,
		Parameters.MinThreshold,
		255);
	PixelSortingAnimation::Reset(Parameters);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingScale(float InScale)
{
	PostProcessParameters.PixelSorting.Scale = FMath::Max(0.0f, InScale);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingColorInterpolationEnabled(bool bEnabled)
{
	PostProcessParameters.PixelSorting.bColorInterpolationEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingTargetColor(const FLinearColor& InTargetColor)
{
	PostProcessParameters.PixelSorting.TargetColor = FLinearColor(
		FMath::Clamp(InTargetColor.R, 0.0f, 1.0f),
		FMath::Clamp(InTargetColor.G, 0.0f, 1.0f),
		FMath::Clamp(InTargetColor.B, 0.0f, 1.0f),
		1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingRowsEnabled(bool bEnabled)
{
	PostProcessParameters.PixelSorting.bSortRows = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingColumnsEnabled(bool bEnabled)
{
	PostProcessParameters.PixelSorting.bSortColumns = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetPixelSortingResolutionDivisor(int32 InDivisor)
{
	PostProcessParameters.PixelSorting.ResolutionDivisor = FMath::Clamp(InDivisor, 1, 8);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurEnabled(bool bEnabled)
{
	PostProcessParameters.ZoomBlur.bEnabled = bEnabled ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurBlackFlushAlpha(float InBlackFlushAlpha)
{
	PostProcessParameters.ZoomBlur.BlackFlushAlpha = FMath::Clamp(InBlackFlushAlpha, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurTriggerThreshold(int32 InTriggerThreshold)
{
	PostProcessParameters.ZoomBlur.TriggerThreshold = FMath::Clamp(
		InTriggerThreshold,
		PostProcessParameters.PixelSorting.MinThreshold,
		255);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurBlackoutStartProgress(float InStartProgress)
{
	PostProcessParameters.ZoomBlur.BlackoutStartProgress = FMath::Clamp(InStartProgress, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurCurve(int32 InCurve)
{
	PostProcessParameters.ZoomBlur.ZoomBlurCurve = FMath::Clamp(
		InCurve,
		static_cast<int32>(EPixelSortingCurve::Linear),
		static_cast<int32>(EPixelSortingCurve::CubicEaseIn));
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurBlackoutCurve(int32 InCurve)
{
	PostProcessParameters.ZoomBlur.BlackoutCurve = FMath::Clamp(
		InCurve,
		static_cast<int32>(EPixelSortingCurve::Linear),
		static_cast<int32>(EPixelSortingCurve::CubicEaseIn));
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurFadeInTimeScale(float InTimeScale)
{
	PostProcessParameters.ZoomBlur.ZoomBlurFadeInTimeScale = FMath::Clamp(InTimeScale, 0.05f, 10.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurFadeOutTimeScale(float InTimeScale)
{
	PostProcessParameters.ZoomBlur.ZoomBlurFadeOutTimeScale = FMath::Clamp(InTimeScale, 0.05f, 10.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurBlackoutFadeInTimeScale(float InTimeScale)
{
	PostProcessParameters.ZoomBlur.BlackoutFadeInTimeScale = FMath::Clamp(InTimeScale, 0.05f, 10.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurBlackoutFadeOutTimeScale(float InTimeScale)
{
	PostProcessParameters.ZoomBlur.BlackoutFadeOutTimeScale = FMath::Clamp(InTimeScale, 0.05f, 10.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurMaximumStrength(float InMaximumStrength)
{
	FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
	ZoomBlur.MaximumStrength = FMath::Clamp(InMaximumStrength, 0.0f, 1.0f);
	ZoomBlur.Strength = ZoomBlur.MaximumStrength * FMath::Clamp(ZoomBlur.Progress, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurStartOffset(float InStartOffset)
{
	PostProcessParameters.ZoomBlur.StartOffset = FMath::Clamp(InStartOffset, 0.0f, 0.99f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurSampleCount(int32 InSampleCount)
{
	PostProcessParameters.ZoomBlur.SampleCount = FMath::Clamp(InSampleCount, 2, 64);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetZoomBlurResolutionDivisor(int32 InDivisor)
{
	PostProcessParameters.ZoomBlur.ResolutionDivisor = FMath::Clamp(InDivisor, 1, 8);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::StartHackPossessionTransition()
{
	if (HackPossessionTransitionPhase != EHackPossessionTransitionPhase::Idle)
	{
		return;
	}

	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::PixelSorting;
	HackTransitionZoomBlurElapsedTime = 0.0f;
	HackTransitionBlackoutElapsedTime = 0.0f;
	bHackTransitionCoveredBroadcastSent = false;

	FPixelSortingParameters& PixelSorting = PostProcessParameters.PixelSorting;
	PixelSorting.bEnabled = true;
	PixelSortingAnimation::Reset(PixelSorting);

	FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
	ZoomBlur.bEnabled = false;
	ZoomBlur.BlackFlushAlpha = 0.0f;
	ZoomBlur.Progress = 0.0f;
	ZoomBlur.Strength = 0.0f;
	ZoomBlur.StartOffset = 0.0f;

	MarkDirty();
	TickFrame();
}

bool ULocalPlayerPostProcessSubsystem::StartHackPossessionReveal()
{
	if (HackPossessionTransitionPhase == EHackPossessionTransitionPhase::RevealFromBlack)
	{
		return true;
	}

	if (HackPossessionTransitionPhase != EHackPossessionTransitionPhase::Covered)
	{
		return false;
	}

	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::RevealFromBlack;
	HackTransitionZoomBlurElapsedTime = 0.0f;
	HackTransitionBlackoutElapsedTime = 0.0f;

	FPixelSortingParameters& PixelSorting = PostProcessParameters.PixelSorting;
	PixelSorting.bEnabled = false;
	PixelSortingAnimation::Reset(PixelSorting);

	FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
	ZoomBlur.bEnabled = true;
	ZoomBlur.BlackFlushAlpha = 1.0f;
	ZoomBlur.Progress = 1.0f;
	ZoomBlur.Strength = FMath::Clamp(ZoomBlur.MaximumStrength, 0.0f, 1.0f);
	ZoomBlur.StartOffset = 0.0f;

	MarkDirty();
	TickFrame();
	return true;
}

void ULocalPlayerPostProcessSubsystem::CancelHackPossessionTransition()
{
	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Idle;
	HackTransitionZoomBlurElapsedTime = 0.0f;
	HackTransitionBlackoutElapsedTime = 0.0f;
	bHackTransitionCoveredBroadcastSent = false;

	FPixelSortingParameters& PixelSorting = PostProcessParameters.PixelSorting;
	PixelSorting.bEnabled = false;
	PixelSortingAnimation::Reset(PixelSorting);

	FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
	ZoomBlur.bEnabled = false;
	ZoomBlur.BlackFlushAlpha = 0.0f;
	ZoomBlur.Progress = 0.0f;
	ZoomBlur.Strength = 0.0f;
	ZoomBlur.StartOffset = 0.0f;

	MarkDirty();
	TickFrame();
}

bool ULocalPlayerPostProcessSubsystem::IsHackPossessionTransitionActive() const
{
	return HackPossessionTransitionPhase != EHackPossessionTransitionPhase::Idle;
}

void ULocalPlayerPostProcessSubsystem::UpdatePixelSorting(float DeltaTime)
{
	FPixelSortingParameters& Parameters = PostProcessParameters.PixelSorting;
	if (Parameters.bEnabled == 0 || DeltaTime <= 0.0f)
	{
		return;
	}

	const float Minimum = static_cast<float>(FMath::Clamp(Parameters.MinThreshold, 0, 255));
	const float ThresholdRange = 255.0f - Minimum;
	const float Speed = FMath::Max(0.0f, Parameters.Scale);
	const float ProgressStep = ThresholdRange > KINDA_SMALL_NUMBER
		? DeltaTime * Speed / ThresholdRange
		: 1.0f;
	const float NextProgress = FMath::Clamp(Parameters.Progress + ProgressStep, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(NextProgress, Parameters.Progress))
	{
		return;
	}

	Parameters.Progress = NextProgress;
	Parameters.Threshold = FMath::Lerp(
		255.0f,
		Minimum,
		PostProcessAnimation::ApplyCurve(NextProgress, Parameters.Curve));

	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::UpdateHackPossessionTransition(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (HackPossessionTransitionPhase == EHackPossessionTransitionPhase::PixelSorting)
	{
		const FPixelSortingParameters& PixelSorting = PostProcessParameters.PixelSorting;
		const int32 TriggerThreshold = FMath::Clamp(
			PostProcessParameters.ZoomBlur.TriggerThreshold,
			PixelSorting.MinThreshold,
			255);
		if (PixelSorting.Threshold > static_cast<float>(TriggerThreshold))
		{
			return;
		}

		HackPossessionTransitionPhase = EHackPossessionTransitionPhase::BlurToBlack;
		HackTransitionZoomBlurElapsedTime = 0.0f;
		HackTransitionBlackoutElapsedTime = 0.0f;
		PostProcessParameters.ZoomBlur.bEnabled = true;
		PostProcessParameters.ZoomBlur.BlackFlushAlpha = 0.0f;
		PostProcessParameters.ZoomBlur.Progress = 0.0f;
		PostProcessParameters.ZoomBlur.Strength = 0.0f;
		PostProcessParameters.ZoomBlur.StartOffset = 0.0f;
		MarkDirty();
		TickFrame();
		return;
	}

	if (HackPossessionTransitionPhase == EHackPossessionTransitionPhase::Covered)
	{
		if (!bHackTransitionCoveredBroadcastSent)
		{
			bHackTransitionCoveredBroadcastSent = true;
			OnHackTransitionCovered.Broadcast();
		}

		return;
	}

	if (HackPossessionTransitionPhase == EHackPossessionTransitionPhase::BlurToBlack)
	{
		FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
		const float ZoomBlurFadeInTimeScale = FMath::Clamp(ZoomBlur.ZoomBlurFadeInTimeScale, 0.05f, 10.0f);
		HackTransitionZoomBlurElapsedTime += DeltaTime * ZoomBlurFadeInTimeScale;
		const float ZoomBlurDuration = FMath::Max(HackTransitionZoomBlurDuration, KINDA_SMALL_NUMBER);
		const float LinearProgress = FMath::Clamp(
			HackTransitionZoomBlurElapsedTime / ZoomBlurDuration,
			0.0f,
			1.0f);
		const float NextProgress = PostProcessAnimation::ApplyCurve(
			LinearProgress,
			ZoomBlur.ZoomBlurCurve);

		float NextBlackoutAlpha = ZoomBlur.BlackFlushAlpha;
		const float BlackoutStartProgress = FMath::Clamp(ZoomBlur.BlackoutStartProgress, 0.0f, 1.0f);
		if (NextProgress >= BlackoutStartProgress)
		{
			const float BlackoutTimeScale = FMath::Clamp(ZoomBlur.BlackoutFadeInTimeScale, 0.05f, 10.0f);
			HackTransitionBlackoutElapsedTime += DeltaTime * BlackoutTimeScale;
			const float BlackoutDuration = FMath::Max(HackTransitionBlackoutDuration, KINDA_SMALL_NUMBER);
			const float LinearBlackoutAlpha = FMath::Clamp(
				HackTransitionBlackoutElapsedTime / BlackoutDuration,
				0.0f,
				1.0f);
			NextBlackoutAlpha = PostProcessAnimation::ApplyCurve(
				LinearBlackoutAlpha,
				ZoomBlur.BlackoutCurve);
		}

		const float MaximumStrength = FMath::Clamp(ZoomBlur.MaximumStrength, 0.0f, 1.0f);
		const float NextStrength = MaximumStrength * NextProgress;
		if (!FMath::IsNearlyEqual(ZoomBlur.Progress, NextProgress)
			|| !FMath::IsNearlyEqual(ZoomBlur.BlackFlushAlpha, NextBlackoutAlpha)
			|| !FMath::IsNearlyEqual(ZoomBlur.Strength, NextStrength))
		{
			ZoomBlur.Progress = NextProgress;
			ZoomBlur.BlackFlushAlpha = NextBlackoutAlpha;
			ZoomBlur.Strength = NextStrength;
			ZoomBlur.StartOffset = 0.0f;
			MarkDirty();
			TickFrame();
		}

		if (NextBlackoutAlpha < 1.0f)
		{
			return;
		}

		ZoomBlur.Progress = 1.0f;
		ZoomBlur.BlackFlushAlpha = 1.0f;
		ZoomBlur.Strength = FMath::Clamp(ZoomBlur.MaximumStrength, 0.0f, 1.0f);
		ZoomBlur.StartOffset = 0.0f;
		FPixelSortingParameters& PixelSorting = PostProcessParameters.PixelSorting;
		PixelSorting.bEnabled = false;
		PixelSortingAnimation::Reset(PixelSorting);
		HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Covered;
		bHackTransitionCoveredBroadcastSent = false;
		MarkDirty();
		TickFrame();
		return;
	}

	if (HackPossessionTransitionPhase != EHackPossessionTransitionPhase::RevealFromBlack)
	{
		return;
	}

	FZoomBlurParameters& ZoomBlur = PostProcessParameters.ZoomBlur;
	const float ZoomBlurFadeOutTimeScale = FMath::Clamp(ZoomBlur.ZoomBlurFadeOutTimeScale, 0.05f, 10.0f);
	const float BlackoutTimeScale = FMath::Clamp(ZoomBlur.BlackoutFadeOutTimeScale, 0.05f, 10.0f);
	HackTransitionZoomBlurElapsedTime += DeltaTime * ZoomBlurFadeOutTimeScale;
	HackTransitionBlackoutElapsedTime += DeltaTime * BlackoutTimeScale;

	const float ZoomBlurDuration = FMath::Max(HackTransitionZoomBlurDuration, KINDA_SMALL_NUMBER);
	const float BlackoutDuration = FMath::Max(HackTransitionBlackoutDuration, KINDA_SMALL_NUMBER);
	const float LinearProgress = 1.0f - FMath::Clamp(
		HackTransitionZoomBlurElapsedTime / ZoomBlurDuration,
		0.0f,
		1.0f);
	const float LinearBlackoutAlpha = 1.0f - FMath::Clamp(
		HackTransitionBlackoutElapsedTime / BlackoutDuration,
		0.0f,
		1.0f);
	const float NextProgress = PostProcessAnimation::ApplyCurve(
		LinearProgress,
		ZoomBlur.ZoomBlurCurve);
	const float NextBlackoutAlpha = PostProcessAnimation::ApplyCurve(
		LinearBlackoutAlpha,
		ZoomBlur.BlackoutCurve);
	const float MaximumStrength = FMath::Clamp(ZoomBlur.MaximumStrength, 0.0f, 1.0f);
	const float NextStrength = MaximumStrength * NextProgress;

	if (!FMath::IsNearlyEqual(ZoomBlur.Progress, NextProgress)
		|| !FMath::IsNearlyEqual(ZoomBlur.BlackFlushAlpha, NextBlackoutAlpha)
		|| !FMath::IsNearlyEqual(ZoomBlur.Strength, NextStrength))
	{
		ZoomBlur.Progress = NextProgress;
		ZoomBlur.BlackFlushAlpha = NextBlackoutAlpha;
		ZoomBlur.Strength = NextStrength;
		ZoomBlur.StartOffset = 0.0f;
		MarkDirty();
		TickFrame();
	}

	if (NextProgress > 0.0f || NextBlackoutAlpha > 0.0f)
	{
		return;
	}

	PostProcessParameters.ZoomBlur.bEnabled = false;
	PostProcessParameters.ZoomBlur.BlackFlushAlpha = 0.0f;
	PostProcessParameters.ZoomBlur.Progress = 0.0f;
	PostProcessParameters.ZoomBlur.Strength = 0.0f;
	PostProcessParameters.ZoomBlur.StartOffset = 0.0f;
	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Idle;
	HackTransitionZoomBlurElapsedTime = 0.0f;
	HackTransitionBlackoutElapsedTime = 0.0f;
	bHackTransitionCoveredBroadcastSent = false;
	MarkDirty();
	TickFrame();
	OnHackTransitionFinished.Broadcast();
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
