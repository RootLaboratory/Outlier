#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "FPostProcessStructures.h"
#include "DeathTransitionSequence.h"
#include "RHIResources.h"
#include "Tickable.h"
#include "LocalPlayerPostProcessSubsystem.generated.h"

class FOutlierPostProcessSceneViewExtension;
class APostProcessVolume;
class UTexture2D;

DECLARE_MULTICAST_DELEGATE(FOnHackTransitionCovered);
DECLARE_MULTICAST_DELEGATE(FOnHackTransitionFinished);
DECLARE_MULTICAST_DELEGATE(FOnDeathBlackNoiseStarted);

// Split Prism 진행 단계.
// Focusing: d(t)가 1 → 0. Settling: (A) 깊이 잔차를 0으로 줄이는 중. Holding: (B) 깊이 잔차를 Stop까지 유지.
enum class ESplitPrismPhase : uint8
{
	Idle,
	Focusing,
	Settling,
	Holding
};

enum class EHackPossessionTransitionPhase : uint8
{
	Idle,
	PixelSorting,
	BlurToBlack,
	Covered,
	RevealFromBlack
};

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

	// 튜닝값은 유지하고 모든 효과의 런타임 상태를 즉시 초기화한다.
	// 리로드 중 사망 연출 / GameOver 암막은 새 Pawn 빙의까지 유지할 수 있다.
	UFUNCTION(BlueprintCallable, Category = "RDG")
	void ResetAllPostProcess(bool bPreserveDeathTransition = false);

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
	void SetOverlayEnabled(bool bEnabled);
	void SetOverlayTintColor(const FLinearColor& InTintColor);
	void SetOverlayGoalValue(float InGoalValue);

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
	void SetPixelSortingColorInterpolationEnabled(bool bEnabled);
	void SetPixelSortingTargetColor(const FLinearColor& InTargetColor);
	void SetPixelSortingRowsEnabled(bool bEnabled);
	void SetPixelSortingColumnsEnabled(bool bEnabled);
	void SetPixelSortingResolutionDivisor(int32 InDivisor);

	void SetZoomBlurEnabled(bool bEnabled);
	void SetZoomBlurBlackFlushAlpha(float InBlackFlushAlpha);
	void SetZoomBlurTriggerThreshold(int32 InTriggerThreshold);
	void SetZoomBlurBlackoutStartProgress(float InStartProgress);
	void SetZoomBlurCurve(int32 InCurve);
	void SetZoomBlurBlackoutCurve(int32 InCurve);
	void SetZoomBlurFadeInTimeScale(float InTimeScale);
	void SetZoomBlurFadeOutTimeScale(float InTimeScale);
	void SetZoomBlurBlackoutFadeInTimeScale(float InTimeScale);
	void SetZoomBlurBlackoutFadeOutTimeScale(float InTimeScale);
	void SetZoomBlurMaximumStrength(float InMaximumStrength);
	void SetZoomBlurStartOffset(float InStartOffset);
	void SetZoomBlurSampleCount(int32 InSampleCount);
	void SetZoomBlurResolutionDivisor(int32 InDivisor);

	// 사망 연출 튜닝값 일괄 갱신. 켜짐 여부와 진행도 / 시간 같은 런타임 필드는 입력값을 무시하고 유지한다.
	void SetDeathNoiseParameters(const FDeathNoiseParameters& InParameters);
	void SetDeathFadeParameters(const FDeathFadeParameters& InParameters);
	void SetDeathBlackParameters(const FDeathBlackParameters& InParameters);
	void SetDeathTransitionTexture(EDeathTransitionTexture Slot, UTexture2D* InTexture);
	UTexture2D* GetDeathTransitionTexture(EDeathTransitionTexture Slot) const { return DeathTransitionTextures[static_cast<int32>(Slot)].Get(); }
	bool IsDeathTransitionTextureReady(EDeathTransitionTexture Slot) const { return DeathTransitionTextureRHIs[static_cast<int32>(Slot)].IsValid(); }
	void SetDeathChromaticAberrationParameters(const FDeathChromaticAberrationParameters& InParameters);

	// 사망 연출(Fade → Black, Noise는 그 위에서 끝까지). 진행 중이면 처음부터 다시 시작한다.
	// 연출을 돌릴 수 없으면 false — 호출자는 기다리지 말고 바로 다음 단계로 넘어가야 한다.
	bool StartDeathTransition();
	void ResetDeathTransition();
	// 사망 연출을 종료하고 기존 ZoomBlur 암막만 켠다. 리로드 중 유지하고 전체 Reset에서 해제한다.
	void StartGameOverBlackout();
	bool IsDeathTransitionActive() const { return DeathTransition.IsActive(); }
	EDeathTransitionPhase GetDeathTransitionPhase() const { return DeathTransition.GetPhase(); }

	// 디버그용 패스별 on/off. 꺼진 패스는 그리기만 빠지고 타임라인은 그대로 흐른다.
	void SetDeathTransitionPassEnabled(EDeathTransitionPass Pass, bool bEnabled);
	bool IsDeathTransitionPassEnabled(EDeathTransitionPass Pass) const { return DeathTransition.IsPassEnabled(Pass); }

	// Black 패스의 Noise 텍스처가 처음 보이는 틱에 한 번 발생한다.
	FOnDeathBlackNoiseStarted OnDeathBlackNoiseStarted;

	// Drone Damage Feedback. 마스크 텍스처와 드러낼 모서리는 Enemy HUD가 정한다. 튜닝값은 RDG Debugger에서 조정한다.
	void SetDroneDamageMaskTexture(EDroneDamageMask Slot, UTexture2D* InTexture);
	UTexture2D* GetDroneDamageMaskTexture(EDroneDamageMask Slot) const { return DroneDamageMaskTextures[static_cast<int32>(Slot)].Get(); }
	bool IsDroneDamageMaskTextureReady(EDroneDamageMask Slot) const { return DroneDamageMaskTextureRHIs[static_cast<int32>(Slot)].IsValid(); }
	void SetDroneDamageMaskRevealed(EDroneDamageMask Slot, bool bRevealed);
	bool IsDroneDamageMaskRevealed(EDroneDamageMask Slot) const;
	// 사망 연출 등으로 HUD가 숨겨진 동안 그리기만 멈춘다. 드러난 상태는 유지한다.
	void SetDroneDamageSuppressed(bool bSuppressed);
	// 마스크 텍스처와 드러난 상태를 모두 지운다.
	void ClearDroneDamage();
	// 튜닝값만 갱신한다. 켜짐 / 드러남 / 시간 같은 런타임 필드는 입력값을 무시하고 유지한다.
	void SetDroneDamageFeedbackParameters(const FDroneDamageFeedbackParameters& InParameters);

	// Split Prism(Slate 갈라짐 + 씬 PP 블러 + 깊이 잔차). 진행 중이면 처음부터 다시 시작한다.
	// FocusDuration 뒤: 깊이 잔차가 꺼져 있으면 바로 끝, A(Settle)는 잔차를 줄이고 끝, B(Persist)는 Stop까지 유지.
	void StartSplitPrism();
	void StopSplitPrism();
	bool IsSplitPrismActive() const { return SplitPrismPhase != ESplitPrismPhase::Idle; }
	ESplitPrismPhase GetSplitPrismPhase() const { return SplitPrismPhase; }
	// 초점이 빗나간 양 d(t). 부호 있음. Focusing이 아니면 0.
	float GetSplitPrismDefocus() const;
	// 깊이 잔차 세기 0~1.
	float GetSplitPrismResidualWeight() const;
	// 고정된 피사체 거리(cm). 아직 못 정했으면 음수.
	float GetSplitPrismSubjectDistance() const { return SplitPrismSubjectDistance; }
	const FSplitPrismSettings& GetSplitPrismSettings() const { return SplitPrismSettings; }
	void SetSplitPrismSettings(const FSplitPrismSettings& InSettings);

	void StartHackPossessionTransition();
	bool StartHackPossessionReveal();
	void CancelHackPossessionTransition();
	bool IsHackPossessionTransitionActive() const;

	FOnHackTransitionCovered OnHackTransitionCovered;
	FOnHackTransitionFinished OnHackTransitionFinished;

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
	friend class FOutlierRDGPostProcessResetTest;

	void MarkDirty();
	void UpdateADSBlur(float DeltaTime);
	void UpdateOverlay(float DeltaTime);
	void UpdatePixelSorting(float DeltaTime);
	void UpdateHackPossessionTransition(float DeltaTime);
	void UpdateDeathNoise(float DeltaTime);
	void UpdateDeathTransition(float DeltaTime);
	void UpdateDroneDamageFeedback(float DeltaTime);
	void RefreshDroneDamageEnabled();
	void RefreshDroneDamageMaskTextures();
	void UpdateSplitPrism(float DeltaTime);
	void ApplySplitPrism();
	void ApplySplitPrismOff();
	void TraceSplitPrismSubjectDistance();
	void EnterSplitPrismPhaseAfterFocus();
	void RefreshDeathTransitionTextures();
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
	uint8 bOverlayRequested : 1 = false;
	uint8 bADSBlurAiming : 1 = false;
	uint8 bADSBlurDebugPassEnabled : 1 = true;

	float ADSBlurElapsedTime = 0.0f;
	float ADSBlurRampInTime = 0.18f;
	float ADSBlurRampOutTime = 0.12f;

	float ADSBlurSocketDistance = 11.0f;

	EHackPossessionTransitionPhase HackPossessionTransitionPhase = EHackPossessionTransitionPhase::Idle;
	float HackTransitionZoomBlurElapsedTime = 0.0f;
	float HackTransitionBlackoutElapsedTime = 0.0f;
	float HackTransitionZoomBlurDuration = 0.35f;
	float HackTransitionBlackoutDuration = 0.35f;
	uint8 bHackTransitionCoveredBroadcastSent : 1 = false;

	// EDroneDamageMask 순서. UPROPERTY 배열 크기는 숫자로 적고, 슬롯 수와 같은지는 cpp에서 검사한다.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DroneDamageMaskTextures[4];
	FTextureRHIRef DroneDamageMaskTextureRHIs[DroneDamageMaskCount];
	uint8 bDroneDamageSuppressed : 1 = false;

	FSplitPrismSettings SplitPrismSettings;
	ESplitPrismPhase SplitPrismPhase = ESplitPrismPhase::Idle;
	// Focusing 경과 시간.
	float SplitPrismElapsedTime = 0.0f;
	// Settling 경과 시간.
	float SplitPrismSettleElapsedTime = 0.0f;
	// D_subj(cm). 음수면 아직 트레이스 전.
	float SplitPrismSubjectDistance = -1.0f;

	FDeathTransitionSequence DeathTransition;
	bool bGameOverBlackoutActive = false;
	// EDeathTransitionTexture 순서. UPROPERTY 배열 크기는 숫자로 적고, 슬롯 수와 같은지는 cpp에서 검사한다.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DeathTransitionTextures[3];
	FTextureRHIRef DeathTransitionTextureRHIs[DeathTransitionTextureCount];

	TWeakObjectPtr<APostProcessVolume> DoFVolume;
	uint8 bADSDoFEnabled : 1 = true;
	float ADSDoFApertureAim = 14.47f;
	float ADSDoFApertureHip = 32.0f;
	float ADSDoFSensorWidth = 12.576f;
	float ADSDoFMinFStop = 0.0f;
	float ADSDoFFocalRegion = 0.0f;
	float ADSDoFFarTransitionRegion = 1500.0f;
};
