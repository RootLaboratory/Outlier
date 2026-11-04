#include "UI/HackEffectTimerWidget.h"

#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "UI/BreathingWidget.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"

namespace
{
	float EvaluateTextFeedbackCurve(const FRuntimeFloatCurve& Curve, float RemainingTime, float Duration)
	{
		if (!FMath::IsFinite(Duration) || Duration <= 0.0f || !FMath::IsFinite(RemainingTime))
		{
			return 0.0f;
		}
		const float RemainingRatio = FMath::Clamp(RemainingTime / Duration, 0.0f, 1.0f);
		const float Fallback = FMath::Square(1.0f - RemainingRatio);
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		const float Value = RichCurve->GetNumKeys() > 0 ? RichCurve->Eval(RemainingRatio) : Fallback;
		// 편집 중 커브가 비거나 접선이 범위를 넘더라도 색상과 주기를 안전한 범위로 유지한다.
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : Fallback;
	}
}

UHackEffectTimerWidget::UHackEffectTimerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 두 커브 모두 기존 (1-남은 비율)^2로 시작한다. 이후 WBP에서 각각의 키와 접선을 편집한다.
	for (FRuntimeFloatCurve* Curve : {&HackTimerTextColorAmountCurve, &HackTimerTextPulseSpeedCurve})
	{
		FRichCurve* RichCurve = Curve->GetRichCurve();
		for (float Ratio : {0.0f, 1.0f})
		{
			const FKeyHandle Handle = RichCurve->AddKey(Ratio, FMath::Square(1.0f - Ratio));
			RichCurve->SetKeyInterpMode(Handle, RCIM_Cubic);
			RichCurve->SetKeyTangentMode(Handle, RCTM_User, false);
			FRichCurveKey& Key = RichCurve->GetKey(Handle);
			Key.ArriveTangent = Key.LeaveTangent = -2.0f * (1.0f - Ratio);
		}
	}
}

void UHackEffectTimerWidget::InitializeForEnemy(AEnemyBase* InEnemy)
{
	// Push 전에 연결된 자식은 부모 Construct에서 다시 전달되어도 위상과 만료 표시를 유지한다.
	if (bHackTimerBound && OwningEnemy.Get() == InEnemy)
	{
		return;
	}
	ReleaseHackTimer();
	OwningEnemy = InEnemy;
	BindHackTimer();
}

void UHackEffectTimerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindHackTimer();
	// 빙의 연출 중에는 시작 이벤트를 기다린다. 비활성 상태를 갱신하면 이벤트 구독도 해제된다.
	// 이미 효과가 시작된 경우에만 공통 배경 위젯에도 현재 상태를 반영한다.
	const AEnemyBase* Enemy = OwningEnemy.Get();
	if (bHackTimerBound && !bHackTimerShowingExpiry && IsValid(Enemy) && Enemy->IsHackEffectActive())
	{
		RefreshHackTimer();
	}
}

void UHackEffectTimerWidget::NativeDestruct()
{
	FinishExpiryDisplay();
	Super::NativeDestruct();
}

void UHackEffectTimerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 숫자 갱신과 색상 연출을 분리한다. 배경이 없어도 타이머 텍스트는 매 프레임 부드럽게 숨쉰다.
	if (IsVisible())
	{
		UpdateHackTimerPulse();
	}
}

float UHackEffectTimerWidget::GetHackTimerBlendAlpha(float RemainingTime, float Duration) const
{
	if (!FMath::IsFinite(Duration) || Duration <= 0.0f || !FMath::IsFinite(RemainingTime))
	{
		return 0.0f;
	}
	float Start = HackTimerColorBlendStartRatio;
	float End = HackTimerColorBlendEndRatio;
	if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || Start > 1.0f || End < 0.0f || End >= Start)
	{
		Start = 0.5f;
		End = 0.15f;
	}
	const float Ratio = FMath::Clamp(RemainingTime / Duration, 0.0f, 1.0f);
	// 잔여 비율은 감소하지만 보간값은 증가해야 한다. 시작 전은 0, 완료 이후는 1로 고정한다.
	return FMath::Clamp((Start - Ratio) / (Start - End), 0.0f, 1.0f);
}

float UHackEffectTimerWidget::GetHackTimerPulsePeriod(float Alpha) const
{
	float Slow = HackTimerPulseSlowPeriod;
	float Fast = HackTimerPulseFastPeriod;
	if (!FMath::IsFinite(Slow) || !FMath::IsFinite(Fast) || Fast <= 0.0f || Fast > Slow)
	{
		Slow = 2.0f;
		Fast = 0.6f;
	}
	return FMath::Lerp(Slow, Fast, Alpha);
}

float UHackEffectTimerWidget::GetHackTimerTextColorAlpha(float RemainingTime, float Duration) const
{
	if (FMath::IsFinite(Duration) && Duration > 0.0f && FMath::IsFinite(RemainingTime) && RemainingTime <= 0.0f)
	{
		return 1.0f;
	}
	// 초반부터 빨갛게 물들지 않도록 숨쉬기 최고점에서 섞이는 경고색의 양도 함께 제한한다.
	const float Wave = 0.5f - 0.5f * FMath::Cos(TextColorPhase * UE_TWO_PI);
	return EvaluateTextFeedbackCurve(HackTimerTextColorAmountCurve, RemainingTime, Duration) * Wave;
}

void UHackEffectTimerWidget::SetHackTimerTextColor(const FLinearColor& Color)
{
	// 한 번 계산한 색을 세 요소에 함께 적용해 분·콜론·초의 숨쉬기와 종료 색상을 일치시킨다.
	for (UTextBlock* Text : {HackEffectMinutesText.Get(), Colon.Get(), HackEffectSecondsText.Get()})
	{
		if (Text)
		{
			Text->SetColorAndOpacity(FSlateColor(Color));
		}
	}
}

void UHackEffectTimerWidget::UpdateHackTimerColor(float RemainingTime, float Duration)
{
	const float Alpha = GetHackTimerBlendAlpha(RemainingTime, Duration);
	const FLinearColor Color = FMath::Lerp(HackTimerNormalColor, HackTimerWarningColor,
		GetHackTimerTextColorAlpha(RemainingTime, Duration));
	SetHackTimerTextColor(Color);
	if (HackEffectPulseWidget)
	{
		const FLinearColor PulseColor = FMath::Lerp(HackTimerPulseNormalColor, HackTimerPulseWarningColor, Alpha);
		HackEffectPulseWidget->SetBreathingState(PulseColor, Alpha, GetHackTimerPulsePeriod(Alpha));
	}
}

void UHackEffectTimerWidget::UpdateHackTimerPulse()
{
	const AEnemyBase* Enemy = OwningEnemy.Get();
	if (!bHackTimerBound || bHackTimerShowingExpiry
		|| !IsValid(Enemy) || !Enemy->IsHackEffectActive())
	{
		return;
	}
	const float RemainingTime = Enemy->GetHackEffectRemainingTime();
	const float Duration = Enemy->GetHackEffectDuration();
	const float SpeedAlpha = EvaluateTextFeedbackCurve(HackTimerTextPulseSpeedCurve, RemainingTime, Duration);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const double Delta = FMath::Max(0.0, Now - LastTextColorTime);
	LastTextColorTime = Now;
	// 게임 시각으로 위상을 누적해 일시정지와 주기 변경에도 색상 흐름을 이어간다.
	TextColorPhase = static_cast<float>(FMath::Frac(TextColorPhase + Delta / GetHackTimerPulsePeriod(SpeedAlpha)));
	UpdateHackTimerColor(RemainingTime, Duration);
}

int32 UHackEffectTimerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Result = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (bHackTimerShowingExpiry && InWidgetStyle.GetColorAndOpacityTint().A > 0.0f)
	{
		bHackTimerExpiryPainted = true;
	}
	return Result;
}

void UHackEffectTimerWidget::BindHackTimer()
{
	AEnemyBase* Enemy = OwningEnemy.Get();
	if (bHackTimerBound)
	{
		return;
	}
	SetHackTimerVisible(false);
	if (!IsValid(Enemy) || (!HackEffectMinutesText && !HackEffectSecondsText))
	{
		return;
	}

	Enemy->OnHackEffectStarted.AddUniqueDynamic(this, &ThisClass::HandleHackEffectStarted);
	Enemy->OnHackEffectEnded.AddUniqueDynamic(this, &ThisClass::HandleHackEffectEnded);
	Enemy->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleHackTargetEndPlay);
	bHackTimerBound = true;
	SetHackTimerTime(FEnemyHackEffectTime());
	SetHackTimerVisible(false);

	// 시작 이벤트보다 HUD 생성이 늦어도 현재 서버 상태부터 이어서 표시한다.
	if (Enemy->IsHackEffectActive())
	{
		HandleHackEffectStarted(Enemy, Enemy->GetHackEffectDuration());
	}
}

void UHackEffectTimerWidget::ReleaseHackTimer()
{
	if (AEnemyBase* Enemy = OwningEnemy.Get())
	{
		Enemy->OnHackEffectStarted.RemoveDynamic(this, &ThisClass::HandleHackEffectStarted);
		Enemy->OnHackEffectEnded.RemoveDynamic(this, &ThisClass::HandleHackEffectEnded);
		Enemy->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleHackTargetEndPlay);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
		World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	}
	bHackTimerBound = false;
	bHackTimerShowingExpiry = false;
	bHackTimerTimeInitialized = false;
	bHackTimerExpiryPainted = false;
	TextColorPhase = 0.0f;
	LastTextColorTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UpdateHackTimerColor(FEnemyHackEffectTime::Duration, FEnemyHackEffectTime::Duration);
	if (HackEffectPulseWidget)
	{
		HackEffectPulseWidget->SetBreathingPlaying(false);
		HackEffectPulseWidget->ResetBreathing();
	}
	OnHackTimerExpiryDisplayFinished.Unbind();
	SetHackTimerVisible(false);
}

void UHackEffectTimerWidget::HandleHackEffectStarted(AEnemyBase* Target, float Duration)
{
	if (!bHackTimerBound || bHackTimerShowingExpiry || Target != OwningEnemy.Get() || !Target->IsHackEffectActive())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	bHackTimerShowingExpiry = false;
	TextColorPhase = 0.0f;
	LastTextColorTime = World->GetTimeSeconds();
	RefreshHackTimer();
	// 로컬 타이머는 다시 그릴 시점만 알린다. 남은 시간은 서버 시각에서 매번 구한다.
	FTimerManagerTimerParameters TimerParameters;
	TimerParameters.bLoop = true;
	TimerParameters.bMaxOncePerFrame = true;
	World->GetTimerManager().SetTimer(HackTimerRefreshHandle, this,
		&ThisClass::RefreshHackTimer, 0.1f, TimerParameters);
}

void UHackEffectTimerWidget::RefreshHackTimer()
{
	// 소유 Controller의 종료 통보가 먼저 오면 Enemy 상태가 아직 활성일 수 있다.
	// 늦은 갱신이 로컬에 보관한 00:00을 이전 남은 시간으로 덮어쓰지 않는다.
	if (bHackTimerShowingExpiry)
	{
		return;
	}
	AEnemyBase* Enemy = OwningEnemy.Get();
	if (!bHackTimerBound || !IsValid(Enemy) || !Enemy->IsHackEffectActive())
	{
		ReleaseHackTimer();
		return;
	}
	SetHackTimerTime(Enemy->GetHackEffectRemainingMinutesAndSeconds());
	UpdateHackTimerColor(Enemy->GetHackEffectRemainingTime(), Enemy->GetHackEffectDuration());
	SetHackTimerVisible(true);
}

void UHackEffectTimerWidget::HandleHackEffectEnded(AEnemyBase* Target, EEnemyHackEffectEndReason EndReason)
{
	if (!bHackTimerBound || Target != OwningEnemy.Get())
	{
		return;
	}
	// Controller와 Enemy 양쪽에서 같은 만료를 통보할 수 있다. 이미 예약한 숨김은 유지한다.
	if (bHackTimerShowingExpiry)
	{
		if (EndReason != EEnemyHackEffectEndReason::Expired)
		{
			FinishExpiryDisplay();
		}
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		ReleaseHackTimer();
		return;
	}
	World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	if (EndReason != EEnemyHackEffectEndReason::Expired)
	{
		ReleaseHackTimer();
		return;
	}

	ShowHackTimerExpiry(Target);
}

void UHackEffectTimerWidget::ShowHackTimerExpiry(AEnemyBase* Target)
{
	if (!Target || Target != OwningEnemy.Get() || !bHackTimerBound || bHackTimerShowingExpiry)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(HackTimerRefreshHandle);
	World->GetTimerManager().ClearTimer(HackTimerHideHandle);
	// 대상 파괴 후에도 마지막 숫자는 위젯이 보관한다. 사망/복귀 처리를 기다리게 하지 않는다.
	SetHackTimerTime(FEnemyHackEffectTime());
	SetHackTimerTextColor(HackTimerWarningColor);
	// 종료 프레임에는 위상과 진폭을 그대로 두어 마지막 밝기를 보존하고 색만 확정한다.
	if (HackEffectPulseWidget)
	{
		HackEffectPulseWidget->FreezeBreathing(HackTimerPulseWarningColor);
	}
	bHackTimerShowingExpiry = true;
	SetHackTimerVisible(true);
	bHackTimerExpiryPainted = false;
	HackTimerZeroFrame = GFrameCounter;
	HackTimerHideHandle = World->GetTimerManager().SetTimerForNextTick(
		this, &ThisClass::HideExpiredHackTimer);
}

void UHackEffectTimerWidget::HideExpiredHackTimer()
{
	if (!bHackTimerShowingExpiry)
	{
		return;
	}
	// 화면에 붙은 HUD는 Paint를 한 번 거쳐야 제거한다. 프레임 번호만으로는 렌더링을 보장할 수 없다.
	// Slate가 없는 단위 테스트/화면 밖 위젯은 기존 프레임 경계를 사용한다.
	if (GFrameCounter <= HackTimerZeroFrame
		|| (CanPaintExpiryDisplay() && !bHackTimerExpiryPainted))
	{
		if (UWorld* World = GetWorld())
		{
			HackTimerHideHandle = World->GetTimerManager().SetTimerForNextTick(
				this, &ThisClass::HideExpiredHackTimer);
		}
		return;
	}
	FinishExpiryDisplay();
}

void UHackEffectTimerWidget::HandleHackTargetEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	if (Actor == OwningEnemy.Get())
	{
		// 자연 만료의 마지막 숫자만 대상과 분리해 둔다. 위젯 제거 시에는 즉시 정리한다.
		if (bHackTimerShowingExpiry)
		{
			return;
		}
		ReleaseHackTimer();
	}
}

void UHackEffectTimerWidget::SetHackTimerTime(const FEnemyHackEffectTime& Time)
{
	if (bHackTimerTimeInitialized && HackEffectTime.Minutes == Time.Minutes && HackEffectTime.Seconds == Time.Seconds)
	{
		return;
	}
	HackEffectTime = Time;
	bHackTimerTimeInitialized = true;
	FNumberFormattingOptions Options;
	Options.SetUseGrouping(false);
	Options.SetMinimumIntegralDigits(2);
	if (HackEffectMinutesText)
	{
		HackEffectMinutesText->SetText(FText::AsNumber(Time.Minutes, &Options));
	}
	if (HackEffectSecondsText)
	{
		HackEffectSecondsText->SetText(FText::AsNumber(Time.Seconds, &Options));
	}
}

void UHackEffectTimerWidget::SetHackTimerVisible(bool bVisible)
{
	// 부모 한 곳의 Visibility로 프레임·콜론·숫자·배경을 함께 제어한다.
	SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (HackEffectPulseWidget)
	{
		HackEffectPulseWidget->SetBreathingPlaying(bVisible && !bHackTimerShowingExpiry);
	}
}

bool UHackEffectTimerWidget::CanPaintExpiryDisplay() const
{
	const TSharedPtr<SWidget> Widget = GetCachedWidget();
	if (!Widget.IsValid() || !IsVisible() || GetRenderOpacity() <= 0.0f || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	FWidgetPath Path;
	if (!FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path))
	{
		return false;
	}
	if (!Path.GetWindow()->IsVisible() || Path.GetWindow()->IsWindowMinimized())
	{
		return false;
	}
	// 자신의 Visibility만으로는 숨겨진 부모와 화면에서 분리된 위젯을 판별할 수 없다.
	FSlateRect VisibleRect = Path.Widgets[Path.Widgets.Num() - 1].Geometry.GetRenderBoundingRect();
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		const FArrangedWidget& Ancestor = Path.Widgets[Index];
		if (Index == 0 || Ancestor.Widget->GetClipping() == EWidgetClipping::ClipToBounds
			|| Ancestor.Widget->GetClipping() == EWidgetClipping::ClipToBoundsAlways)
		{
			VisibleRect = VisibleRect.IntersectionWith(Ancestor.Geometry.GetRenderBoundingRect());
		}
		if (Ancestor.Widget->GetRenderOpacity() <= 0.0f)
		{
			return false;
		}
	}
	return VisibleRect.Right > VisibleRect.Left && VisibleRect.Bottom > VisibleRect.Top;
}

void UHackEffectTimerWidget::FinishExpiryDisplay()
{
	// 정리가 재진입해도 완료 통지는 한 번만 보낸다.
	const FSimpleDelegate DisplayFinished = bHackTimerShowingExpiry
		? OnHackTimerExpiryDisplayFinished : FSimpleDelegate();
	ReleaseHackTimer();
	DisplayFinished.ExecuteIfBound();
}
