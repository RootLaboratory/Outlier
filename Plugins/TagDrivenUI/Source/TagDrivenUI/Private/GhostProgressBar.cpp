#include "GhostProgressBar.h"

#include "Engine/World.h"
#include "SGhostProgressBar.h"

DEFINE_LOG_CATEGORY_STATIC(LogGhostProgressBar, Log, All);

void UGhostProgressBar::ValidateTiming()
{
	const bool bValid = FMath::IsFinite(GhostDelay) && FMath::IsFinite(GhostDuration)
		&& GhostDelay >= 0.0f && GhostDuration >= 0.0f;
	EffectiveDelay = FMath::IsFinite(GhostDelay) ? FMath::Max(0.0f, GhostDelay) : 0.25f;
	EffectiveDuration = FMath::IsFinite(GhostDuration) ? FMath::Max(0.0f, GhostDuration) : 0.60f;
	if (!bValid && !bInvalidTimingWarningLogged)
	{
		UE_LOG(LogGhostProgressBar, Warning, TEXT("[%s] Invalid Ghost timing; using safe Delay/Duration."), *GetName());
		bInvalidTimingWarningLogged = true;
	}
}

void UGhostProgressBar::SetPercentImmediate(float InPercent)
{
	const float Target = FMath::IsFinite(InPercent) ? FMath::Clamp(InPercent, 0.0f, 1.0f) : 0.0f;
	bHasReceivedValue = true;
	bAnimating = false;
	Elapsed = 0.0;
	GhostPercent = GhostStartPercent = Target;
	Super::SetPercent(Target);
	RefreshSlateAppearance();
	RefreshSlatePercent();
	if (GhostWidget) GhostWidget->StopAnimation();
}

void UGhostProgressBar::SetTargetPercent(float InPercent)
{
	if (!FMath::IsFinite(InPercent) || !bHasReceivedValue || !bEnableGhost)
	{
		SetPercentImmediate(InPercent);
		return;
	}
	const float Target = FMath::Clamp(InPercent, 0.0f, 1.0f);
	RefreshSlateAppearance();
	if (Target == GetPercent()) return;

	// 현재 화면의 잔상을 보존한다. 감소 중 재피격/회복 모두 새 목표에서 Delay를 다시 시작한다.
	Super::SetPercent(Target);
	GhostPercent = FMath::Max(GhostPercent, Target);
	GhostStartPercent = GhostPercent;
	Elapsed = 0.0;
	ValidateTiming();
	if (GhostPercent <= Target || (EffectiveDelay == 0.0f && EffectiveDuration == 0.0f))
	{
		SetPercentImmediate(Target);
		return;
	}
	BeginGhostAnimation();
}

void UGhostProgressBar::BeginGhostAnimation()
{
	bAnimating = true;
	if (UWorld* World = AnimationWorld.Get()) LastGameTime = World->GetTimeSeconds();
	RefreshSlateAppearance();
	RefreshSlatePercent();
	if (GhostWidget && AnimationWorld.IsValid()) GhostWidget->StartAnimation();
}

void UGhostProgressBar::ResetGhostToCurrent()
{
	SetPercentImmediate(GetPercent());
}

void UGhostProgressBar::SetGhostEnabled(bool bEnabled)
{
	bEnableGhost = bEnabled;
	ResetGhostToCurrent();
	RefreshSlateAppearance();
}

void UGhostProgressBar::SetGhostTiming(float InDelay, float InDuration)
{
	GhostDelay = InDelay;
	GhostDuration = InDuration;
	ValidateTiming();
	if (bAnimating)
	{
		if (EffectiveDelay == 0.0f && EffectiveDuration == 0.0f)
		{
			ResetGhostToCurrent();
			return;
		}
		GhostStartPercent = GhostPercent;
		Elapsed = 0.0;
		BeginGhostAnimation();
	}
}

void UGhostProgressBar::AdvanceGhostAnimation(float DeltaTime)
{
	if (!bAnimating || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f) return;
	if (!bEnableGhost)
	{
		ResetGhostToCurrent();
		return;
	}

	// 경과 시간 하나로 Delay 경계를 넘긴 잔여 시간도 보간에 반영한다.
	Elapsed += DeltaTime;
	if (Elapsed < EffectiveDelay) return;
	const double Alpha = EffectiveDuration > 0.0f
		? FMath::Clamp((Elapsed - EffectiveDelay) / EffectiveDuration, 0.0, 1.0) : 1.0;
	const double Remaining = 1.0 - Alpha;
	const double EasedAlpha = 1.0 - Remaining * Remaining * Remaining;
	GhostPercent = FMath::Max(GetPercent(), static_cast<float>(FMath::Lerp(static_cast<double>(GhostStartPercent), static_cast<double>(GetPercent()), EasedAlpha)));
	if (Alpha >= 1.0) ResetGhostToCurrent();
	else RefreshSlatePercent();
}

void UGhostProgressBar::TickGhostFromGameTime()
{
	UWorld* World = AnimationWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		ResetGhostToCurrent();
		return;
	}
	const double CurrentGameTime = World->GetTimeSeconds();
	const double Delta = CurrentGameTime - LastGameTime;
	LastGameTime = CurrentGameTime;
	// Slate의 실시간 Delta를 사용하지 않는다. Pause/시간 배율은 World 게임 시간에 따른다.
	AdvanceGhostAnimation(static_cast<float>(Delta));
}

void UGhostProgressBar::RefreshSlatePercent()
{
	if (GhostWidget) GhostWidget->SetGhostPercent(GhostPercent);
}

void UGhostProgressBar::RefreshSlateAppearance()
{
	if (GhostWidget) GhostWidget->RefreshAppearance();
}

TSharedRef<SWidget> UGhostProgressBar::RebuildWidget()
{
	AnimationWorld = GetWorld();
	GhostWidget = SNew(SGhostProgressBar).Owner(this);
	MyProgressBar = GhostWidget->GetMainBar();
	return GhostWidget.ToSharedRef();
}

void UGhostProgressBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ValidateTiming();
	RefreshSlateAppearance();
	// 디자이너/Slate 재생성은 즉시 맞추되, 아직 실제 수신이 없다면 첫 수신 즉시 정책을 유지한다.
	const bool bHadValue = bHasReceivedValue;
	ResetGhostToCurrent();
	bHasReceivedValue = bHadValue;
}

void UGhostProgressBar::ReleaseSlateResources(bool bReleaseChildren)
{
	ResetGhostToCurrent();
	GhostWidget.Reset();
	AnimationWorld.Reset();
	bHasReceivedValue = false;
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UGhostProgressBar::SetVisibility(ESlateVisibility InVisibility)
{
	if (InVisibility == ESlateVisibility::Hidden || InVisibility == ESlateVisibility::Collapsed)
		ResetGhostToCurrent();
	Super::SetVisibility(InVisibility);
}

void UGhostProgressBar::RemoveFromParent()
{
	ResetGhostToCurrent();
	Super::RemoveFromParent();
}
