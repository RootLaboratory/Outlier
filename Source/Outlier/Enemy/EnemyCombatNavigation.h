#pragma once

#include "CoreMinimal.h"

// 후보 생성과 비용 비교만 담당한다. 실제 사선/경로/방 경계/점유 검사는 호출자가 수행한다.
namespace EnemyCombatNavigation
{
	struct FCandidate
	{
		FVector Destination = FVector::ZeroVector;
		bool bProgressOnly = false;
	};

	OUTLIER_API void BuildCandidates(const FVector& Start, const FVector& Target, float EffectiveRange,
		bool bLeft, TArray<FCandidate>& OutCandidates);
	OUTLIER_API double EstimateAttackSeconds(double PathLength, double InitialSpeed, double MaxSpeed,
		double Acceleration, double RotationDegrees, double RotationSpeed, double PrepRemaining,
		double RecoveryRemaining, double CadenceRemaining, bool bDirectlyVisible);
	OUTLIER_API bool IsBetterCandidate(double Seconds, double DistanceError, double BestSeconds,
		double BestDistanceError, double TieTolerance = 0.1);
	OUTLIER_API bool ShouldReplacePath(double CurrentSeconds, double NewSeconds,
		double MinimumGain = 0.3, bool bCurrentValid = true);
}
