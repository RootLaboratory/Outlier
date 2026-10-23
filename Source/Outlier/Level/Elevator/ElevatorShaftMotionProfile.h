#pragma once

#include "CoreMinimal.h"

class UCurveFloat;

// 초기화 때 속도와 누적 적분을 캐시하고, 모든 머신이 같은 서버 시각으로 거리를 평가한다.
class OUTLIER_API FElevatorShaftSpeedProfile
{
public:
	bool Build(const UCurveFloat* Curve, bool bAcceleration, FString& Error);
	double Speed(double Alpha) const;
	double Integral(double Alpha) const;
	double TotalIntegral() const { return Areas.IsEmpty() ? 0.0 : Areas.Last(); }

	static double FinishBoundary(double Distance, double Height, double Tolerance = 0.1);

private:
	static constexpr int32 SampleIntervals = 1024;
	TArray<double> Speeds;
	TArray<double> Areas;
};
