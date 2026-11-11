#pragma once

#include "CoreMinimal.h"

namespace OutlierEnemySight
{
	inline constexpr double DefaultAwarenessFillDuration = 1.0;
	inline constexpr double DefaultAwarenessDecayDuration = 2.0;
	inline constexpr double DefaultHearingAwarenessRatio = 0.25;
	inline constexpr double DefaultHearingAwarenessHoldDuration = 0.5;
	OUTLIER_API double ResolveHearingAwarenessHoldDuration(double Duration);
	OUTLIER_API double ResolveSightFillDuration(double Distance, double NearDuration, double FarDuration,
		double NearDistance, double FarDistance);

	// 등록 검증을 마친 서로 다른 본 개수를 받는다. 실제 본 목록 검증은 호출부의 책임이다.
	OUTLIER_API bool MeetsExposureThreshold(int32 RegisteredBoneCount, int32 VisibleBoneCount, bool bRegisteredHeadVisible = false);
}

// 시각·청각이 공유하는 최초 발견 한 번의 계산 상태다. Actor 참조나 전투 전환은 보관하지 않는다.
struct OUTLIER_API FEnemySightAwareness
{
	// 새 관측 수명을 시작한다. 같은 Alert Task 재진입마다 호출하는 용도가 아니다.
	bool Reset(double ObservationTime, bool bVisible);

	// 직전 관측 상태로 현재 시각까지 계산한 뒤 새 가시 상태를 저장한다.
	// 관측이 바뀐 시각에도 호출해야 큰 갱신 간격 안의 노출/은폐 시간을 구분할 수 있다.
	// HearingExpiresAt은 마지막 소리의 인지 만료 시각이다. 음수면 청각 입력이 없으며 시각과 중복 합산하지 않는다.
	// 100%에 처음 도달한 호출만 true를 반환하며, 완료 상태는 Reset 전까지 유지한다.
	// FillDuration은 청각 기준 시간이다. SightFillDuration을 주면 이번 관측부터의 시각 속도만 바뀐다.
	// 생략(-1)하면 기존 고정 속도를 사용한다. 직전 구간에 새 거리의 속도를 소급하지 않는다.
	bool Observe(
		double ObservationTime,
		bool bVisible,
		double FillDuration = OutlierEnemySight::DefaultAwarenessFillDuration,
		double DecayDuration = OutlierEnemySight::DefaultAwarenessDecayDuration,
		double HearingExpiresAt = -1.0,
		double HearingRatio = OutlierEnemySight::DefaultHearingAwarenessRatio,
		double SightFillDuration = -1.0);

	double GetAwareness() const { return Awareness; }
	bool IsComplete() const { return bComplete; }
	bool WasCompletedFromHearing() const { return bComplete && bCompletedFromHearing; }
	bool IsHearingActive(double ObservationTime) const { return PreviousHearingExpiresAt >= 0.0 && ObservationTime < PreviousHearingExpiresAt; }

private:
	double Awareness = 0.0;
	double LastObservationTime = 0.0;
	bool bInitialized = false;
	bool bPreviouslyVisible = false;
	double PreviousHearingExpiresAt = -1.0;
	double PreviousSightFillDuration = -1.0;
	bool bComplete = false;
	bool bCompletedFromHearing = false;
};
