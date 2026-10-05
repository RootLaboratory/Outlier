#include "ShieldDamageFeedbackWidget.h"

void UShieldDamageFeedbackWidget::NativeOnInitialized()
{
	// 기존 BP 가 FeedbackDuration 만 조정했다면 새 FadeIn/FadeOut 의 합에 반영한다.
	// 새 Fade 설정을 직접 지정한 BP 는 그대로 사용한다.
	constexpr float DefaultFadeInTime = 0.1f;
	constexpr float DefaultFadeOutTime = 0.4f;
	constexpr float DefaultFeedbackDuration = DefaultFadeInTime + DefaultFadeOutTime;
	if (FMath::IsNearlyEqual(FadeInTime, DefaultFadeInTime)
		&& FMath::IsNearlyEqual(FadeOutTime, DefaultFadeOutTime)
		&& !FMath::IsNearlyEqual(FeedbackDuration, DefaultFeedbackDuration))
	{
		const float LegacyDuration = FMath::Max(0.0f, FeedbackDuration);
		FadeInTime = FMath::Min(DefaultFadeInTime, LegacyDuration);
		FadeOutTime = LegacyDuration - FadeInTime;
	}

	Super::NativeOnInitialized();
}
