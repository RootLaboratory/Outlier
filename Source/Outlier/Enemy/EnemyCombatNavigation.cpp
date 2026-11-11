#include "Enemy/EnemyCombatNavigation.h"

namespace EnemyCombatNavigation
{
void BuildCandidates(const FVector& Start, const FVector& Target, float EffectiveRange,
	bool bLeft, TArray<FCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(EffectiveRange) || EffectiveRange <= 0.0f)
	{
		return;
	}
	FVector Radial = (Start - Target).GetSafeNormal2D();
	if (Radial.IsNearlyZero())
	{
		Radial = FVector::ForwardVector;
	}
	const double CurrentDistance = FVector::Dist2D(Start, Target);
	const double NearRadius = FMath::Clamp(CurrentDistance, 100.0, FMath::Max(100.0, double(EffectiveRange) * 0.75));
	const double FarRadius = FMath::Max(NearRadius, double(EffectiveRange) * 0.95);
	const double Side = bLeft ? -1.0 : 1.0;
	auto AddUnique = [&OutCandidates](const FVector& Destination, bool bProgressOnly)
	{
		if (!OutCandidates.ContainsByPredicate([&Destination](const FCandidate& Candidate)
			{ return Candidate.Destination.Equals(Destination, 1.0); }))
		{
			OutCandidates.Add({Destination, bProgressOnly});
		}
	};
	// 접근/양쪽 우회/후퇴를 한 집합으로 만든다. 의도별로 예산을 다시 만들지 않는다.
	for (const double Angle : {0.0, 20.0, -20.0, 45.0, -45.0, 75.0, -75.0, 110.0, -110.0})
	{
		for (const double Radius : {NearRadius, FarRadius})
		{
			FVector Location = Target + Radial.RotateAngleAxis(Angle * Side, FVector::UpVector) * Radius;
			Location.Z = Start.Z;
			AddUnique(Location, false);
		}
	}
	// 아래 후보는 사격 위치가 아니다. 안전 경로가 검증된 경우에만 다음 관측 각도로 이동한다.
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Radial) * Side;
	for (const double Offset : {250.0, -250.0, 500.0, -500.0})
	{
		AddUnique(Start + Lateral * Offset, true);
	}
	const double ApproachDistance = FMath::Max(0.0, CurrentDistance - 150.0);
	AddUnique(Start - Radial * FMath::Min(300.0, ApproachDistance), true);
	AddUnique(Start - Radial * FMath::Min(600.0, ApproachDistance), true);
}

double EstimateAttackSeconds(double PathLength, double InitialSpeed, double MaxSpeed,
	double Acceleration, double RotationDegrees, double RotationSpeed, double PrepRemaining,
	double RecoveryRemaining, double CadenceRemaining, bool bDirectlyVisible)
{
	if (!FMath::IsFinite(PathLength) || !FMath::IsFinite(InitialSpeed) || !FMath::IsFinite(MaxSpeed)
		|| !FMath::IsFinite(Acceleration) || !FMath::IsFinite(RotationDegrees) || !FMath::IsFinite(RotationSpeed)
		|| !FMath::IsFinite(PrepRemaining) || !FMath::IsFinite(RecoveryRemaining) || !FMath::IsFinite(CadenceRemaining))
	{
		return TNumericLimits<double>::Max();
	}
	const double Distance = FMath::Max(0.0, PathLength);
	const double Speed = FMath::Max(0.0, MaxSpeed);
	const double Initial = FMath::Clamp(InitialSpeed, 0.0, Speed);
	double Travel = 0.0;
	if (Distance > UE_SMALL_NUMBER)
	{
		if (Speed <= UE_SMALL_NUMBER)
		{
			return TNumericLimits<double>::Max();
		}
		if (Acceleration > UE_SMALL_NUMBER)
		{
			const double AcceleratingDistance = (Speed * Speed - Initial * Initial) / (2.0 * Acceleration);
			Travel = Distance <= AcceleratingDistance
				? (FMath::Sqrt(Initial * Initial + 2.0 * Acceleration * Distance) - Initial) / Acceleration
				: (Speed - Initial) / Acceleration + (Distance - AcceleratingDistance) / Speed;
		}
		else if (Initial > UE_SMALL_NUMBER)
		{
			Travel = Distance / Initial;
		}
		else
		{
			return TNumericLimits<double>::Max();
		}
	}
	const double Rotation = FMath::Abs(RotationDegrees) <= UE_SMALL_NUMBER ? 0.0
		: RotationSpeed > UE_SMALL_NUMBER ? FMath::Abs(RotationDegrees) / RotationSpeed : TNumericLimits<double>::Max();
	// 이동/회전/회복은 병렬이다. 가려진 동안에는 전조를 차감하지 않아 이동 뒤에 남은 준비를 붙인다.
	const double Preparation = FMath::Max(0.0, PrepRemaining) + (bDirectlyVisible ? 0.0 : Travel);
	return FMath::Max(FMath::Max(Travel, Rotation),
		FMath::Max(Preparation, FMath::Max(FMath::Max(0.0, RecoveryRemaining), FMath::Max(0.0, CadenceRemaining))));
}

bool IsBetterCandidate(double Seconds, double DistanceError, double BestSeconds, double BestDistanceError, double TieTolerance)
{
	if (!FMath::IsFinite(Seconds) || Seconds == TNumericLimits<double>::Max())
	{
		return false;
	}
	if (!FMath::IsFinite(BestSeconds) || BestSeconds == TNumericLimits<double>::Max())
	{
		return true;
	}
	const double Tolerance = FMath::Max(0.0, TieTolerance);
	if (FMath::Abs(Seconds - BestSeconds) <= Tolerance)
	{
		return FMath::Abs(DistanceError) < FMath::Abs(BestDistanceError);
	}
	return Seconds < BestSeconds;
}

bool ShouldReplacePath(double CurrentSeconds, double NewSeconds, double MinimumGain, bool bCurrentValid)
{
	if (!FMath::IsFinite(NewSeconds) || NewSeconds == TNumericLimits<double>::Max())
	{
		return false;
	}
	return !bCurrentValid || !FMath::IsFinite(CurrentSeconds)
		|| NewSeconds + FMath::Max(0.0, MinimumGain) <= CurrentSeconds;
}
}
