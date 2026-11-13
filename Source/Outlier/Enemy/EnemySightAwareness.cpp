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

double OutlierEnemySight::ResolveSightFillDuration(double Distance, double NearDuration, double FarDuration,
	double NearDistance, double FarDistance)
{
	const double SafeFarDuration = ResolveDuration(FarDuration, DefaultAwarenessFillDuration, TEXT("fill"));
	if (!FMath::IsFinite(NearDuration) || NearDuration <= 0.0 || NearDuration > SafeFarDuration
		|| !FMath::IsFinite(NearDistance) || NearDistance < 0.0
		|| !FMath::IsFinite(FarDistance) || FarDistance <= NearDistance)
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness sight distance settings; using default near curve."));
		NearDuration = FMath::Min(0.25, SafeFarDuration);
		NearDistance = 300.0;
		FarDistance = 1500.0;
	}
	if (!FMath::IsFinite(Distance) || Distance < 0.0)
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness sight distance; using far duration."));
		return SafeFarDuration;
	}
	const double Alpha = FMath::Clamp((Distance - NearDistance) / (FarDistance - NearDistance), 0.0, 1.0);
	return FMath::Lerp(NearDuration, SafeFarDuration, Alpha);
}

bool OutlierEnemySight::MeetsExposureThreshold(int32 RegisteredBoneCount, int32 VisibleBoneCount, bool bRegisteredHeadVisible)
{
	if (RegisteredBoneCount <= 0 || VisibleBoneCount < 0 || VisibleBoneCount > RegisteredBoneCount)
	{
		return false;
	}

	// N+1 대신 나눗셈과 나머지로 올림해 큰 개수에서도 정수 overflow를 피한다.
	return (bRegisteredHeadVisible && VisibleBoneCount > 0)
		|| VisibleBoneCount >= RegisteredBoneCount / 2 + RegisteredBoneCount % 2;
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
	PreviousSightFillDuration = -1.0;
	bComplete = false;
	bCompletedFromHearing = false;
	return true;
}

bool FEnemySightAwareness::Observe(
	double ObservationTime, bool bVisible, double FillDuration, double DecayDuration,
	double HearingExpiresAt, double HearingRatio, double SightFillDuration)
{
	if (!FMath::IsFinite(ObservationTime)
		|| !FMath::IsFinite(HearingExpiresAt)
		|| (bInitialized && (ObservationTime < LastObservationTime
			|| !FMath::IsFinite(ObservationTime - LastObservationTime))))
	{
		UE_LOG(LogEnemySightAwareness, Warning, TEXT("Invalid awareness observation time; observation ignored."));
		return false;
	}

	const double SafeFillDuration = ResolveDuration(
		FillDuration, OutlierEnemySight::DefaultAwarenessFillDuration, TEXT("fill"));
	const bool bDistanceBasedSight = SightFillDuration != -1.0;
	const double SafeSightFillDuration = bDistanceBasedSight
		? ResolveDuration(SightFillDuration, SafeFillDuration, TEXT("sight fill")) : SafeFillDuration;
	if (!bInitialized)
	{
		// 최초 관측 전의 시간은 보였는지 알 수 없으므로 누적하지 않는다.
		Reset(ObservationTime, bVisible);
		PreviousHearingExpiresAt = HearingExpiresAt;
		PreviousSightFillDuration = SafeSightFillDuration;
		return false;
	}
	if (bComplete)
	{
		return false;
	}

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
	// 거리 관측은 구간 끝에 도착한다. 이번 거리는 다음 구간에 저장하고 지난 구간은 지난 속도로 계산한다.
	// 고정 시간 호출자는 기존 설정 변경 동작을 유지한다.
	const double IntervalSightFillDuration = bDistanceBasedSight && PreviousSightFillDuration > 0.0
		? PreviousSightFillDuration : SafeSightFillDuration;
	const double Gain = bPreviouslyVisible ? ElapsedTime / IntervalSightFillDuration
		: HearingTime * SafeHearingRatio / SafeFillDuration;
	const double PeakAwareness = Awareness + Gain;
	const bool bFilledByHearing = !bPreviouslyVisible;
	const double Decay = bPreviouslyVisible ? 0.0 : (ElapsedTime - HearingTime) / SafeDecayDuration;
	Awareness = FMath::Clamp(PeakAwareness - Decay, 0.0, 1.0);
	LastObservationTime = ObservationTime;
	bPreviouslyVisible = bVisible;
	PreviousHearingExpiresAt = HearingExpiresAt;
	PreviousSightFillDuration = SafeSightFillDuration;

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
