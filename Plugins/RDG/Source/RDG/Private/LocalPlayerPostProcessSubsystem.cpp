#include "LocalPlayerPostProcessSubsystem.h"

#include "OutlierPostProcessSceneViewExtension.h"
#include "RDGExplosionVolumeProvider.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"
#include "Engine/LocalPlayer.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"

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

	// 기존 렌즈 CA는 여기서 켜지 않는다. Shipping에서만 Possess 시점에 켜고 사망 / 월드 이탈 시 끈다
	// (AFirstPersonPlayerController). 이 서브시스템은 LocalPlayer 소속이라 로비에서도 살아 있기 때문.

	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::Deinitialize()
{
	OnHackTransitionCovered.Clear();
	OnHackTransitionFinished.Clear();
	OnDeathBlackNoiseStarted.Clear();
	DeathTransition.Reset(PostProcessParameters, UIPostProcessParameters);
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
	// 텍스처 리소스는 지정 직후엔 아직 없을 수 있어서, 준비될 때까지 매 틱 확인한다.
	RefreshDeathTransitionTextures();
	RefreshDroneDamageMaskTextures();

	UpdateOverlay(DeltaTime);
	UpdateADSBlur(DeltaTime);
	UpdatePixelSorting(DeltaTime);
	UpdateHackPossessionTransition(DeltaTime);
	UpdateDeathTransition(DeltaTime);
	UpdateDeathNoise(DeltaTime);
	UpdateDroneDamageFeedback(DeltaTime);
	UpdateSplitPrism(DeltaTime);
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

void ULocalPlayerPostProcessSubsystem::ResetAllPostProcess(bool bPreserveDeathTransition)
{
	PlayerState = FPPGameplayState();
	bOverlayRequested = false;
	bADSBlurAiming = false;
	ADSBlurElapsedTime = 0.0f;
	HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Idle;
	HackTransitionZoomBlurElapsedTime = 0.0f;
	HackTransitionBlackoutElapsedTime = 0.0f;
	bHackTransitionCoveredBroadcastSent = false;
	bSplitPrismActive = false;
	SplitPrismElapsedTime = 0.0f;

	PostProcessParameters.MotionBlur.bEnabled = false;
	PostProcessParameters.LensFlare.bEnabled = false;
	PostProcessParameters.BloomBlur.bEnabled = false;
	PostProcessParameters.DualKawaseBlur.bEnabled = false;
	PostProcessParameters.Datamoshing.bEnabled = false;
	PostProcessParameters.Datamoshing.Progress = 0.0f;
	PostProcessParameters.PixelSorting.bEnabled = false;
	PixelSortingAnimation::Reset(PostProcessParameters.PixelSorting);
	PostProcessParameters.ZoomBlur.bEnabled = false;
	PostProcessParameters.ZoomBlur.Progress = 0.0f;
	PostProcessParameters.ZoomBlur.BlackFlushAlpha = 0.0f;
	PostProcessParameters.ZoomBlur.Strength = 0.0f;
	PostProcessParameters.ADSBlur.bEnabled = false;
	PostProcessParameters.SplitPrismDefocus.bEnabled = false;
	PostProcessParameters.SplitPrismDefocus.Defocus = 0.0f;
	UIPostProcessParameters.ChromaticAberration.bEnabled = false;
	UIPostProcessParameters.Overlay.bEnabled = false;
	UIPostProcessParameters.Overlay.AccumulatedValue = 0.0f;
	UIPostProcessParameters.SplitPrism.bEnabled = false;
	UIPostProcessParameters.SplitPrism.Offset = 0.0f;
	bDroneDamageSuppressed = false;
	PostProcessParameters.DroneDamageFeedback.bEnabled = false;
	PostProcessParameters.DroneDamageFeedback.Time = 0.0f;
	for (float& MaskWeight : PostProcessParameters.DroneDamageFeedback.MaskWeights)
	{
		MaskWeight = 0.0f;
	}
	if (!bPreserveDeathTransition)
	{
		DeathTransition.Reset(PostProcessParameters, UIPostProcessParameters);
	}

	UpdateDepthOfField();
	MarkDirty();
	TickFrame();
	if (ViewExtension.IsValid())
	{
		// 렌더 스레드 캐시는 해당 스레드에서만 지운다. 명령 완료까지 확장을 유지한다.
		const auto Extension = ViewExtension;
		ENQUEUE_RENDER_COMMAND(ResetOutlierPostProcessHistory)(
			[Extension](FRHICommandListImmediate&)
			{
				Extension->ResetRuntimeHistory_RenderThread();
			});
	}
	UE_LOG(LogTemp, Log, TEXT("[PostProcessReset][RDG] PreserveDeathTransition=%d"), bPreserveDeathTransition);
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

void ULocalPlayerPostProcessSubsystem::SetOverlayEnabled(bool bEnabled)
{
	bOverlayRequested = bEnabled;
	if (bEnabled)
	{
		// 0에서 올라가는 첫 프레임부터 패스를 그래프에 유지한다.
		UIPostProcessParameters.Overlay.bEnabled = true;
	}
	else if (UIPostProcessParameters.Overlay.AccumulatedValue <= KINDA_SMALL_NUMBER)
	{
		UIPostProcessParameters.Overlay.AccumulatedValue = 0.0f;
		UIPostProcessParameters.Overlay.bEnabled = false;
	}
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetOverlayTintColor(const FLinearColor& InTintColor)
{
	UIPostProcessParameters.Overlay.TintColor = InTintColor.GetClamped(0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetOverlayGoalValue(float InGoalValue)
{
	UIPostProcessParameters.Overlay.GoalValue = FMath::Clamp(InGoalValue, 0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::UpdateOverlay(float DeltaTime)
{
	FOverlayParameters& Overlay = UIPostProcessParameters.Overlay;
	if (DeltaTime <= 0.0f || (!bOverlayRequested && Overlay.bEnabled == 0))
	{
		return;
	}

	const float TargetValue = bOverlayRequested
		? FMath::Clamp(Overlay.GoalValue, 0.0f, 1.0f)
		: 0.0f;
	const float NextValue = FMath::FInterpConstantTo(
		Overlay.AccumulatedValue,
		TargetValue,
		DeltaTime,
		1.0f);
	const bool bReachedZero = !bOverlayRequested && NextValue <= KINDA_SMALL_NUMBER;
	const int32 NextEnabled = bReachedZero ? 0 : 1;

	if (FMath::IsNearlyEqual(Overlay.AccumulatedValue, NextValue)
		&& Overlay.bEnabled == NextEnabled)
	{
		return;
	}

	Overlay.AccumulatedValue = bReachedZero ? 0.0f : NextValue;
	Overlay.bEnabled = NextEnabled;
	MarkDirty();
	TickFrame();
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

void ULocalPlayerPostProcessSubsystem::UpdateDeathNoise(float DeltaTime)
{
	FDeathNoiseParameters& DeathNoise = PostProcessParameters.DeathNoise;
	if (DeathNoise.bEnabled == 0 || DeltaTime <= 0.0f)
	{
		return;
	}

	DeathNoise.Time += DeltaTime;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDeathNoiseParameters(const FDeathNoiseParameters& InParameters)
{
	FDeathNoiseParameters& DeathNoise = PostProcessParameters.DeathNoise;
	DeathNoise.MaxIntensity = FMath::Max(0.0f, InParameters.MaxIntensity);
	DeathNoise.bRampIntensity = InParameters.bRampIntensity ? 1 : 0;
	DeathNoise.RampDuration = FMath::Max(0.0f, InParameters.RampDuration);
	DeathNoise.Seed = InParameters.Seed;
	DeathNoise.SliceRows = FMath::Clamp(InParameters.SliceRows, 1.0f, 1024.0f);
	DeathNoise.SliceSplitChance = FMath::Clamp(InParameters.SliceSplitChance, 0.0f, 1.0f);
	DeathNoise.GlitchRate = FMath::Clamp(InParameters.GlitchRate, 1.0f, 120.0f);
	DeathNoise.GlitchStrength = FMath::Max(0.0f, InParameters.GlitchStrength);
	DeathNoise.GlitchThreshold = FMath::Clamp(InParameters.GlitchThreshold, 0.0f, 1.0f);
	DeathNoise.GlitchGlow = FMath::Max(0.0f, InParameters.GlitchGlow);
	DeathNoise.BurstChance = FMath::Clamp(InParameters.BurstChance, 0.0f, 1.0f);
	DeathNoise.BurstStrength = FMath::Max(1.0f, InParameters.BurstStrength);
	DeathNoise.BurstThreshold = FMath::Clamp(InParameters.BurstThreshold, 0.0f, 1.0f);
	DeathNoise.Tint = InParameters.Tint.GetClamped(0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDeathFadeParameters(const FDeathFadeParameters& InParameters)
{
	FDeathFadeParameters& DeathFade = PostProcessParameters.DeathFade;
	DeathFade.TargetColor = InParameters.TargetColor.GetClamped(0.0f, 1.0f);
	DeathFade.MaxStrength = FMath::Clamp(InParameters.MaxStrength, 0.0f, 1.0f);
	DeathFade.Duration = FMath::Max(0.0f, InParameters.Duration);
	DeathFade.bEaseIn = InParameters.bEaseIn ? 1 : 0;
	DeathFade.EaseInPower = FMath::Clamp(InParameters.EaseInPower, 1.0f, 16.0f);
	DeathFade.Delay = FMath::Max(0.0f, InParameters.Delay);
	DeathFade.VignetteDelay = FMath::Max(0.0f, InParameters.VignetteDelay);
	DeathFade.VignetteFadeDuration = FMath::Max(0.0f, InParameters.VignetteFadeDuration);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDeathBlackParameters(const FDeathBlackParameters& InParameters)
{
	FDeathBlackParameters& DeathBlack = PostProcessParameters.DeathBlack;
	for (int32 LayerIndex = 0; LayerIndex < DeathBlackLayerCount; ++LayerIndex)
	{
		DeathBlack.LayerDelays[LayerIndex] = FMath::Max(0.0f, InParameters.LayerDelays[LayerIndex]);
		DeathBlack.LayerFadeDurations[LayerIndex] = FMath::Max(0.0f, InParameters.LayerFadeDurations[LayerIndex]);
	}
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDeathTransitionTexture(EDeathTransitionTexture Slot, UTexture2D* InTexture)
{
	if (Slot >= EDeathTransitionTexture::Count)
	{
		return;
	}

	DeathTransitionTextures[static_cast<int32>(Slot)] = InTexture;
	RefreshDeathTransitionTextures();
}

void ULocalPlayerPostProcessSubsystem::RefreshDeathTransitionTextures()
{
	static_assert(UE_ARRAY_COUNT(DeathTransitionTextures) == DeathTransitionTextureCount, "DeathTransitionTextures must match EDeathTransitionTexture");

	for (int32 SlotIndex = 0; SlotIndex < DeathTransitionTextureCount; ++SlotIndex)
	{
		const UTexture2D* Texture = DeathTransitionTextures[SlotIndex];
		const FTextureRHIRef NewTextureRHI = Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
		if (DeathTransitionTextureRHIs[SlotIndex] == NewTextureRHI)
		{
			continue;
		}

		DeathTransitionTextureRHIs[SlotIndex] = NewTextureRHI;
		if (ViewExtension.IsValid())
		{
			const TSharedPtr<FOutlierPostProcessSceneViewExtension, ESPMode::ThreadSafe> TargetViewExtension = ViewExtension;
			const EDeathTransitionTexture Slot = static_cast<EDeathTransitionTexture>(SlotIndex);
			ENQUEUE_RENDER_COMMAND(UpdateDeathTransitionTexture)(
				[TargetViewExtension, Slot, NewTextureRHI](FRHICommandListImmediate&)
				{
					TargetViewExtension->UpdateDeathTransitionTexture(Slot, NewTextureRHI);
				});
		}
	}
}

void ULocalPlayerPostProcessSubsystem::SetDeathChromaticAberrationParameters(const FDeathChromaticAberrationParameters& InParameters)
{
	FDeathChromaticAberrationParameters& DeathChromatic = UIPostProcessParameters.DeathChromaticAberration;
	DeathChromatic.OffsetX = FMath::Clamp(InParameters.OffsetX, -0.1f, 0.1f);
	DeathChromatic.OffsetY = FMath::Clamp(InParameters.OffsetY, -0.1f, 0.1f);
	MarkDirty();
	TickFrame();
}

bool ULocalPlayerPostProcessSubsystem::StartDeathTransition()
{
	RefreshDeathTransitionTextures();
	// 텍스처가 없으면 Noise 레이어를 보여줄 수 없으므로 호출자가 프리셋 UI로 바로 넘어간다.
	if (!ViewExtension.IsValid()
		|| !IsDeathTransitionTextureReady(EDeathTransitionTexture::BlackBackground)
		|| !IsDeathTransitionTextureReady(EDeathTransitionTexture::BlackNoise))
	{
		return false;
	}

	DeathTransition.Start(PostProcessParameters, UIPostProcessParameters);
	MarkDirty();
	TickFrame();
	return true;
}

void ULocalPlayerPostProcessSubsystem::ResetDeathTransition()
{
	DeathTransition.Reset(PostProcessParameters, UIPostProcessParameters);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDeathTransitionPassEnabled(EDeathTransitionPass Pass, bool bEnabled)
{
	DeathTransition.SetPassEnabled(Pass, bEnabled, PostProcessParameters, UIPostProcessParameters);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::UpdateDeathTransition(float DeltaTime)
{
	bool bBlackNoiseStarted = false;
	if (!DeathTransition.Tick(DeltaTime, PostProcessParameters, UIPostProcessParameters, bBlackNoiseStarted))
	{
		return;
	}

	MarkDirty();
	TickFrame();

	if (bBlackNoiseStarted)
	{
		OnDeathBlackNoiseStarted.Broadcast();
	}
}

void ULocalPlayerPostProcessSubsystem::SetDroneDamageMaskTexture(EDroneDamageMask Slot, UTexture2D* InTexture)
{
	if (Slot >= EDroneDamageMask::Count)
	{
		return;
	}

	DroneDamageMaskTextures[static_cast<int32>(Slot)] = InTexture;
	RefreshDroneDamageMaskTextures();
}

void ULocalPlayerPostProcessSubsystem::RefreshDroneDamageMaskTextures()
{
	static_assert(UE_ARRAY_COUNT(DroneDamageMaskTextures) == DroneDamageMaskCount, "DroneDamageMaskTextures must match EDroneDamageMask");

	for (int32 SlotIndex = 0; SlotIndex < DroneDamageMaskCount; ++SlotIndex)
	{
		const UTexture2D* Texture = DroneDamageMaskTextures[SlotIndex];
		const FTextureRHIRef NewTextureRHI = Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
		if (DroneDamageMaskTextureRHIs[SlotIndex] == NewTextureRHI)
		{
			continue;
		}

		DroneDamageMaskTextureRHIs[SlotIndex] = NewTextureRHI;
		if (ViewExtension.IsValid())
		{
			const TSharedPtr<FOutlierPostProcessSceneViewExtension, ESPMode::ThreadSafe> TargetViewExtension = ViewExtension;
			const EDroneDamageMask Slot = static_cast<EDroneDamageMask>(SlotIndex);
			ENQUEUE_RENDER_COMMAND(UpdateDroneDamageMaskTexture)(
				[TargetViewExtension, Slot, NewTextureRHI](FRHICommandListImmediate&)
				{
					TargetViewExtension->UpdateDroneDamageMaskTexture(Slot, NewTextureRHI);
				});
		}
	}
}

void ULocalPlayerPostProcessSubsystem::SetDroneDamageMaskRevealed(EDroneDamageMask Slot, bool bRevealed)
{
	if (Slot >= EDroneDamageMask::Count)
	{
		return;
	}

	PostProcessParameters.DroneDamageFeedback.MaskWeights[static_cast<int32>(Slot)] = bRevealed ? 1.0f : 0.0f;
	RefreshDroneDamageEnabled();
}

bool ULocalPlayerPostProcessSubsystem::IsDroneDamageMaskRevealed(EDroneDamageMask Slot) const
{
	return Slot < EDroneDamageMask::Count
		&& PostProcessParameters.DroneDamageFeedback.MaskWeights[static_cast<int32>(Slot)] > 0.0f;
}

void ULocalPlayerPostProcessSubsystem::SetDroneDamageSuppressed(bool bSuppressed)
{
	if (bDroneDamageSuppressed == bSuppressed)
	{
		return;
	}

	bDroneDamageSuppressed = bSuppressed;
	RefreshDroneDamageEnabled();
}

void ULocalPlayerPostProcessSubsystem::ClearDroneDamage()
{
	for (int32 SlotIndex = 0; SlotIndex < DroneDamageMaskCount; ++SlotIndex)
	{
		DroneDamageMaskTextures[SlotIndex] = nullptr;
		PostProcessParameters.DroneDamageFeedback.MaskWeights[SlotIndex] = 0.0f;
	}
	bDroneDamageSuppressed = false;
	RefreshDroneDamageMaskTextures();
	RefreshDroneDamageEnabled();
}

void ULocalPlayerPostProcessSubsystem::RefreshDroneDamageEnabled()
{
	FDroneDamageFeedbackParameters& DroneDamage = PostProcessParameters.DroneDamageFeedback;

	bool bAnyRevealed = false;
	for (const float MaskWeight : DroneDamage.MaskWeights)
	{
		bAnyRevealed |= MaskWeight > 0.0f;
	}

	DroneDamage.bEnabled = bAnyRevealed && !bDroneDamageSuppressed ? 1 : 0;
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::SetDroneDamageFeedbackParameters(const FDroneDamageFeedbackParameters& InParameters)
{
	FDroneDamageFeedbackParameters& DroneDamage = PostProcessParameters.DroneDamageFeedback;
	DroneDamage.bSliceGlitch = InParameters.bSliceGlitch ? 1 : 0;
	DroneDamage.bPixelSort = InParameters.bPixelSort ? 1 : 0;
	DroneDamage.bShiftToBlack = InParameters.bShiftToBlack ? 1 : 0;
	DroneDamage.Intensity = FMath::Max(0.0f, InParameters.Intensity);
	DroneDamage.Seed = InParameters.Seed;
	DroneDamage.SliceRows = FMath::Clamp(InParameters.SliceRows, 1.0f, 1024.0f);
	DroneDamage.SliceSplitChance = FMath::Clamp(InParameters.SliceSplitChance, 0.0f, 1.0f);
	DroneDamage.GlitchRate = FMath::Clamp(InParameters.GlitchRate, 1.0f, 120.0f);
	DroneDamage.GlitchStrength = FMath::Max(0.0f, InParameters.GlitchStrength);
	DroneDamage.GlitchThreshold = FMath::Clamp(InParameters.GlitchThreshold, 0.0f, 1.0f);
	DroneDamage.GlitchGlow = FMath::Max(0.0f, InParameters.GlitchGlow);
	DroneDamage.BurstChance = FMath::Clamp(InParameters.BurstChance, 0.0f, 1.0f);
	DroneDamage.BurstStrength = FMath::Max(1.0f, InParameters.BurstStrength);
	DroneDamage.BurstThreshold = FMath::Clamp(InParameters.BurstThreshold, 0.0f, 1.0f);
	DroneDamage.PixelSortMode = FMath::Clamp(
		InParameters.PixelSortMode,
		static_cast<int32>(EPixelSortingMode::White),
		static_cast<int32>(EPixelSortingMode::Dark));
	DroneDamage.PixelSortThreshold = FMath::Clamp(InParameters.PixelSortThreshold, 0.0f, 255.0f);
	DroneDamage.bPixelSortRows = InParameters.bPixelSortRows ? 1 : 0;
	DroneDamage.bPixelSortColumns = InParameters.bPixelSortColumns ? 1 : 0;
	DroneDamage.PixelSortResolutionDivisor = FMath::Clamp(InParameters.PixelSortResolutionDivisor, 1, 8);
	DroneDamage.ShiftOffsetX = FMath::Clamp(InParameters.ShiftOffsetX, -0.5f, 0.5f);
	DroneDamage.ShiftOffsetY = FMath::Clamp(InParameters.ShiftOffsetY, -0.5f, 0.5f);
	DroneDamage.ShiftJitter = FMath::Clamp(InParameters.ShiftJitter, 0.0f, 0.2f);
	DroneDamage.MaskGlow = FMath::Max(0.0f, InParameters.MaskGlow);
	DroneDamage.Tint = InParameters.Tint.GetClamped(0.0f, 1.0f);
	MarkDirty();
	TickFrame();
}

void ULocalPlayerPostProcessSubsystem::UpdateDroneDamageFeedback(float DeltaTime)
{
	FDroneDamageFeedbackParameters& DroneDamage = PostProcessParameters.DroneDamageFeedback;
	if (DroneDamage.bEnabled == 0 || DeltaTime <= 0.0f)
	{
		return;
	}

	DroneDamage.Time += DeltaTime;
	MarkDirty();
	TickFrame();
}

namespace SplitPrismAnimation
{
	constexpr int32 MaxOvershootCount = 3;

	// 지나침 한 번이 끝날 때마다 다음 지나침 깊이에 곱하는 값.
	constexpr float OvershootDecay = 0.35f;

	// 초점이 빗나간 양 d(Alpha). 꼭짓점 1 → -A → +A·k → ... → 0 을 차례로 잇는다.
	// 구간 길이는 꼭짓점 사이 거리의 제곱근에 비례해서, 작은 보정일수록 상대적으로 천천히 움직인다.
	// 첫 구간(링을 돌려 지나치는 동작)은 감속 곡선, 이후 보정 구간은 가속 → 감속.
	float EvaluateDefocus(const FSplitPrismSettings& Settings, float Alpha)
	{
		float Peaks[MaxOvershootCount + 2];
		int32 PeakCount = 0;
		Peaks[PeakCount++] = 1.0f;

		const int32 OvershootCount = Settings.OvershootAmount > 0.0f
			? FMath::Clamp(Settings.OvershootCount, 0, MaxOvershootCount)
			: 0;
		float Amount = Settings.OvershootAmount;
		for (int32 Index = 0; Index < OvershootCount; ++Index)
		{
			Peaks[PeakCount++] = (Index % 2 == 0 ? -Amount : Amount);
			Amount *= OvershootDecay;
		}
		Peaks[PeakCount++] = 0.0f;

		float SegmentLengths[MaxOvershootCount + 1];
		float TotalLength = 0.0f;
		for (int32 Index = 0; Index < PeakCount - 1; ++Index)
		{
			SegmentLengths[Index] = FMath::Sqrt(FMath::Abs(Peaks[Index + 1] - Peaks[Index]));
			TotalLength += SegmentLengths[Index];
		}

		float Remaining = FMath::Clamp(Alpha, 0.0f, 1.0f) * TotalLength;
		for (int32 Index = 0; Index < PeakCount - 1; ++Index)
		{
			const bool bLastSegment = Index == PeakCount - 2;
			if (Remaining <= SegmentLengths[Index] || bLastSegment)
			{
				const float SegmentAlpha = SegmentLengths[Index] > 0.0f
					? FMath::Clamp(Remaining / SegmentLengths[Index], 0.0f, 1.0f)
					: 1.0f;
				const float Eased = Index == 0
					? 1.0f - FMath::Pow(1.0f - SegmentAlpha, Settings.FocusEasePower)
					: FMath::SmoothStep(0.0f, 1.0f, SegmentAlpha);
				return FMath::Lerp(Peaks[Index], Peaks[Index + 1], Eased);
			}
			Remaining -= SegmentLengths[Index];
		}

		return 0.0f;
	}
}

void ULocalPlayerPostProcessSubsystem::StartSplitPrism()
{
	bSplitPrismActive = true;
	SplitPrismElapsedTime = 0.0f;
	ApplySplitPrismDefocus(GetSplitPrismDefocus());
}

void ULocalPlayerPostProcessSubsystem::StopSplitPrism()
{
	if (!bSplitPrismActive)
	{
		return;
	}

	bSplitPrismActive = false;
	ApplySplitPrismDefocus(0.0f);
}

float ULocalPlayerPostProcessSubsystem::GetSplitPrismDefocus() const
{
	if (!bSplitPrismActive || SplitPrismSettings.FocusDuration <= 0.0f)
	{
		return 0.0f;
	}

	return SplitPrismAnimation::EvaluateDefocus(
		SplitPrismSettings,
		SplitPrismElapsedTime / SplitPrismSettings.FocusDuration);
}

void ULocalPlayerPostProcessSubsystem::SetSplitPrismSettings(const FSplitPrismSettings& InSettings)
{
	SplitPrismSettings.FocusDuration = FMath::Max(InSettings.FocusDuration, 0.01f);
	SplitPrismSettings.FocusEasePower = FMath::Clamp(InSettings.FocusEasePower, 0.1f, 8.0f);
	SplitPrismSettings.OvershootAmount = FMath::Clamp(InSettings.OvershootAmount, 0.0f, 0.5f);
	SplitPrismSettings.OvershootCount = FMath::Clamp(InSettings.OvershootCount, 0, SplitPrismAnimation::MaxOvershootCount);
	// 0.25면 확대 배율이 0.5까지 내려간다. 그 이상은 화면이 너무 커진다.
	SplitPrismSettings.StartOffset = FMath::Clamp(InSettings.StartOffset, 0.0f, 0.25f);
	SplitPrismSettings.MaxBlurRadius = FMath::Clamp(InSettings.MaxBlurRadius, 0.0f, 64.0f);
	SplitPrismSettings.BlurSampleCount = FMath::Clamp(InSettings.BlurSampleCount, 8, 128);
	// 0.5 이하라 R/B 반경 배율(1 ± Fringe)이 항상 양수다.
	SplitPrismSettings.FringeAmount = FMath::Clamp(InSettings.FringeAmount, 0.0f, 0.5f);
	SplitPrismSettings.BokehRimBias = FMath::Clamp(InSettings.BokehRimBias, 0.0f, 1.0f);

	// 진행 중이면 바뀐 값을 바로 보이게 한다.
	if (bSplitPrismActive)
	{
		ApplySplitPrismDefocus(GetSplitPrismDefocus());
	}
}

void ULocalPlayerPostProcessSubsystem::UpdateSplitPrism(float DeltaTime)
{
	if (!bSplitPrismActive)
	{
		return;
	}

	SplitPrismElapsedTime += DeltaTime;
	if (SplitPrismElapsedTime >= SplitPrismSettings.FocusDuration)
	{
		bSplitPrismActive = false;
	}

	ApplySplitPrismDefocus(GetSplitPrismDefocus());
}

void ULocalPlayerPostProcessSubsystem::ApplySplitPrismDefocus(float Defocus)
{
	FSplitPrismParameters& SplitPrism = UIPostProcessParameters.SplitPrism;
	SplitPrism.Offset = SplitPrismSettings.StartOffset * Defocus;
	SplitPrism.bEnabled = SplitPrism.Offset != 0.0f;

	FSplitPrismDefocusParameters& Blur = PostProcessParameters.SplitPrismDefocus;
	Blur.Defocus = Defocus;
	Blur.MaxRadius = SplitPrismSettings.MaxBlurRadius;
	Blur.SampleCount = SplitPrismSettings.BlurSampleCount;
	Blur.FringeAmount = SplitPrismSettings.FringeAmount;
	Blur.RimBias = SplitPrismSettings.BokehRimBias;
	Blur.bEnabled = Defocus != 0.0f && Blur.MaxRadius > 0.0f;

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
	PostProcessParameters.ADSBlur.FocusDistanceWorld = FMath::Max(0.0f, InFocusDistanceWorld);
	MarkDirty();
	TickFrame();
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
