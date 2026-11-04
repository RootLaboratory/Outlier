#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Curves/CurveFloat.h"
#include "Enemy/EnemyHackEffectTypes.h"
#include "HackEffectTimerWidget.generated.h"

class AEnemyBase;
class UTextBlock;
class UImage;
class UBreathingWidget;

// 프레임·숫자·배경을 한 묶음으로 표시하는 해킹 수명 UI. 게임플레이 시간은 대상이 소유한다.
UCLASS()
class OUTLIER_API UHackEffectTimerWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UHackEffectTimerWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Hack Timer")
	void InitializeForEnemy(AEnemyBase* InEnemy);
	void ReleaseHackTimer();
	void ShowHackTimerExpiry(AEnemyBase* Target);
	bool IsShowingHackTimerExpiry() const { return bHackTimerShowingExpiry; }
	FSimpleDelegate OnHackTimerExpiryDisplayFinished;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// WBP에서 두 TextBlock을 같은 이름으로 만들고 그 사이에 ':'를 배치한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UTextBlock> HackEffectMinutesText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UTextBlock> HackEffectSecondsText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UTextBlock> Colon;

	// 프레임은 점멸시키지 않고 숫자와 같은 시작/종료 시점에만 표시한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UImage> TimerFrame;

	// 공통 위젯은 재질과 위상을 관리하고 타이머는 긴박도만 전달한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Enemy HUD|Hack Timer")
	TObjectPtr<UBreathingWidget> HackEffectPulseWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer")
	FLinearColor HackTimerNormalColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer")
	FLinearColor HackTimerWarningColor = FLinearColor::Red;

	// 타이머 텍스트의 기본색/경고색과 별개로 프레임 발광의 색을 보간한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer|Pulse")
	FLinearColor HackTimerPulseNormalColor = FLinearColor(FColor(105, 241, 223));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer|Pulse")
	FLinearColor HackTimerPulseWarningColor = FLinearColor(FColor(255, 100, 108));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HackTimerColorBlendStartRatio = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HackTimerColorBlendEndRatio = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer", meta = (ClampMin = "0.001", Units = "s"))
	float HackTimerPulseSlowPeriod = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer", meta = (ClampMin = "0.001", Units = "s"))
	float HackTimerPulseFastPeriod = 0.6f;

	// X: 남은 비율(1=시작, 0=만료). Y: 숨쉬기 최고점의 경고색 비중(0~1).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer|Text Curves",
		meta = (ToolTip = "X: remaining ratio (1=start, 0=expiry). Y: maximum warning color blend (0..1)."))
	FRuntimeFloatCurve HackTimerTextColorAmountCurve;

	// X: 남은 비율. Y: 주기 보간(0=SlowPeriod, 1=FastPeriod). 색상 커브와 독립적으로 조절한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy HUD|Hack Timer|Text Curves",
		meta = (ToolTip = "X: remaining ratio (1=start, 0=expiry). Y: speed blend (0=SlowPeriod, 1=FastPeriod)."))
	FRuntimeFloatCurve HackTimerTextPulseSpeedCurve;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Enemy HUD|Hack Timer")
	FEnemyHackEffectTime HackEffectTime;

private:
	friend class FEnemyHackEffectHUDTest;
	friend class FEnemyHackEffectAssetBindingTest;
	void BindHackTimer();
	void RefreshHackTimer();
	void SetHackTimerTime(const FEnemyHackEffectTime& Time);
	void SetHackTimerVisible(bool bVisible);
	void HideExpiredHackTimer();
	float GetHackTimerBlendAlpha(float RemainingTime, float Duration) const;
	float GetHackTimerPulsePeriod(float Alpha) const;
	float GetHackTimerTextColorAlpha(float RemainingTime, float Duration) const;
	void SetHackTimerTextColor(const FLinearColor& Color);
	void UpdateHackTimerColor(float RemainingTime, float Duration);
	void UpdateHackTimerPulse();

	UFUNCTION()
	void HandleHackEffectStarted(AEnemyBase* Target, float Duration);

	UFUNCTION()
	void HandleHackEffectEnded(AEnemyBase* Target, EEnemyHackEffectEndReason EndReason);

	UFUNCTION()
	void HandleHackTargetEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	FTimerHandle HackTimerRefreshHandle;
	FTimerHandle HackTimerHideHandle;
	float TextColorPhase = 0.0f;
	double LastTextColorTime = 0.0;
	uint64 HackTimerZeroFrame = 0;
	mutable bool bHackTimerExpiryPainted = false;
	bool bHackTimerBound = false;
	bool bHackTimerShowingExpiry = false;
	bool bHackTimerTimeInitialized = false;

	void FinishExpiryDisplay();
	bool CanPaintExpiryDisplay() const;
	TWeakObjectPtr<AEnemyBase> OwningEnemy;
};
