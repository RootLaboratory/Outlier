#include "Enemy/EnemySightAwareness.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemySightAwareness, Log, All);

namespace
{
	double ResolveDuration(double Duration, double DefaultDuration, const TCHAR* Name)
	{
		if (FMath::IsFinite(Duration) && Duration > 0.0)
		{
			return Duration;
		}

		UE_LOG(LogEnemySightAwareness, Warning,
			TEXT("Invalid awareness %s duration; using %.1f seconds."), Name, DefaultDuration);
		return DefaultDuration;
	}
}

double OutlierEnemySight::ResolveHearingAwarenessHoldDuration(double Duration)
{
	return ResolveDuration(Duration, DefaultHearingAwarenessHoldDuration, TEXT("hearing hold"));
}

bool OutlierEnemySight::MeetsExposureThreshold(int32 RegisteredBoneCount, int32 VisibleBoneCount)
{
	if (RegisteredBoneCount <= 0 || VisibleBoneCount < 0 || VisibleBoneCount > RegisteredBoneCount)
	{
		return false;
	}

	// N+1 대신 나눗셈과 나머지로 올림해 큰 개수에서도 정수 overflow를 피한다.
	return VisibleBoneCount >= RegisteredBoneCount / 2 + RegisteredBoneCount % 2;
}

bool FEnemySightAwareness::Reset(double ObservationTime, bool bVisible)
{
	if (!FMath::IsFinite(ObservationTime))
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness observation time; reset ignored."));
		return false;
	}

	Awareness = 0.0;
	LastObservationTime = ObservationTime;
	bInitialized = true;
	bPreviouslyVisible = bVisible;
	PreviousHearingExpiresAt = -1.0;
	bComplete = false;
	bCompletedFromHearing = false;
	return true;
}

bool FEnemySightAwareness::Observe(
	double ObservationTime, bool bVisible, double FillDuration, double DecayDuration,
	double HearingExpiresAt, double HearingRatio)
{
	if (!FMath::IsFinite(ObservationTime)
		|| !FMath::IsFinite(HearingExpiresAt)
		|| (bInitialized && (ObservationTime < LastObservationTime
			|| !FMath::IsFinite(ObservationTime - LastObservationTime))))
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness observation time; observation ignored."));
		return false;
	}

	if (!bInitialized)
	{
		// 최초 관측 전의 시간은 보였는지 알 수 없으므로 누적하지 않는다.
		Reset(ObservationTime, bVisible);
		PreviousHearingExpiresAt = HearingExpiresAt;
		return false;
	}
	if (bComplete)
	{
		return false;
	}

	const double SafeFillDuration = ResolveDuration(
		FillDuration, OutlierEnemySight::DefaultAwarenessFillDuration, TEXT("fill"));
	const double SafeDecayDuration = ResolveDuration(
		DecayDuration, OutlierEnemySight::DefaultAwarenessDecayDuration, TEXT("decay"));
	const double ElapsedTime = ObservationTime - LastObservationTime;
	double SafeHearingRatio = HearingRatio;
	if (!FMath::IsFinite(SafeHearingRatio) || SafeHearingRatio <= 0.0 || SafeHearingRatio > 1.0)
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness hearing ratio; using 0.25."));
		SafeHearingRatio = OutlierEnemySight::DefaultHearingAwarenessRatio;
	}
	// 한 갱신 안에서 청각이 만료돼도 인지 구간과 감소 구간을 나눈다.
	// 먼저 100%를 채웠다면 이후 은폐 시간이 포함되어 있어도 완료를 놓치지 않는다.
	const double HearingTime = PreviousHearingExpiresAt < 0.0 ? 0.0
		: FMath::Clamp(PreviousHearingExpiresAt - LastObservationTime, 0.0, ElapsedTime);
	const double Gain = bPreviouslyVisible ? ElapsedTime / SafeFillDuration
		: HearingTime * SafeHearingRatio / SafeFillDuration;
	const double PeakAwareness = Awareness + Gain;
	const bool bFilledByHearing = !bPreviouslyVisible;
	const double Decay = bPreviouslyVisible ? 0.0 : (ElapsedTime - HearingTime) / SafeDecayDuration;
	Awareness = FMath::Clamp(PeakAwareness - Decay, 0.0, 1.0);
	LastObservationTime = ObservationTime;
	bPreviouslyVisible = bVisible;
	PreviousHearingExpiresAt = HearingExpiresAt;

	// 프레임별 나눗셈의 반올림 때문에 정확한 1초 경계가 다음 프레임으로 밀리지 않게 한다.
	constexpr double BoundaryTolerance = 1.e-9;
	if (PeakAwareness >= 1.0 - BoundaryTolerance)
	{
		Awareness = 1.0;
		bComplete = true;
		bCompletedFromHearing = bFilledByHearing;
		return true;
	}
	if (Awareness <= BoundaryTolerance)
	{
		Awareness = 0.0;
	}
	return false;
}
