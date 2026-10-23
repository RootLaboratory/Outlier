#include "Level/Elevator/ElevatorShaftMotionProfile.h"
#include "Curves/CurveFloat.h"

bool FElevatorShaftSpeedProfile::Build(const UCurveFloat* Curve, bool bAcceleration, FString& Error)
{
	Speeds.Reset();
	Areas.Reset();
	Error.Reset();
	if (!Curve)
	{
		Error = TEXT("A normalized speed CurveFloat is required.");
		return false;
	}

	float MinTime = 0.f;
	float MaxTime = 0.f;
	Curve->FloatCurve.GetTimeRange(MinTime, MaxTime);
	if (!FMath::IsNearlyZero(MinTime, 1.e-4f) || !FMath::IsNearlyEqual(MaxTime, 1.f, 1.e-4f))
	{
		Error = TEXT("Speed curve time range must be 0 to 1.");
		return false;
	}

	TArray<double> Samples;
	Samples.Reserve(SampleIntervals + 1);
	for (int32 Index = 0; Index <= SampleIntervals; ++Index)
	{
		const double Value = Curve->GetFloatValue(static_cast<float>(Index) / SampleIntervals);
		if (!FMath::IsFinite(Value) || Value < -1.e-5 || Value > 1.0 + 1.e-5)
		{
			Error = TEXT("Speed curve must contain finite values between 0 and 1.");
			return false;
		}
		if (Index > 0 && (bAcceleration ? Value < Samples.Last() - 1.e-5 : Value > Samples.Last() + 1.e-5))
		{
			Error = TEXT("Acceleration must be monotonic increasing; deceleration must be monotonic decreasing.");
			return false;
		}
		Samples.Add(FMath::Clamp(Value, 0.0, 1.0));
	}

	if (!FMath::IsNearlyEqual(Samples[0], bAcceleration ? 0.0 : 1.0, 1.e-4)
		|| !FMath::IsNearlyEqual(Samples.Last(), bAcceleration ? 1.0 : 0.0, 1.e-4))
	{
		Error = TEXT("Speed curve endpoints must be 0/1 for acceleration or 1/0 for deceleration.");
		return false;
	}
	// 등속/정지 경계에서 가속도가 튀지 않도록 양 끝 속도 접선도 검증한다.
	if (FMath::Abs(Samples[1] - Samples[0]) * SampleIntervals > 0.05
		|| FMath::Abs(Samples.Last() - Samples[SampleIntervals - 1]) * SampleIntervals > 0.05)
	{
		Error = TEXT("Speed curve needs flat endpoint tangents for smooth acceleration transitions.");
		return false;
	}

	Samples[0] = bAcceleration ? 0.0 : 1.0;
	Samples.Last() = bAcceleration ? 1.0 : 0.0;
	Speeds = MoveTemp(Samples);
	Areas.SetNumUninitialized(SampleIntervals + 1);
	Areas[0] = 0.0;
	for (int32 Index = 1; Index <= SampleIntervals; ++Index)
	{
		Areas[Index] = Areas[Index - 1] + (Speeds[Index - 1] + Speeds[Index]) * (0.5 / SampleIntervals);
	}
	return true;
}

double FElevatorShaftSpeedProfile::Speed(double Alpha) const
{
	if (Speeds.IsEmpty() || !FMath::IsFinite(Alpha)) return 0.0;
	const double Position = FMath::Clamp(Alpha, 0.0, 1.0) * SampleIntervals;
	const int32 Index = FMath::Min(FMath::FloorToInt(Position), SampleIntervals - 1);
	return FMath::Lerp(Speeds[Index], Speeds[Index + 1], Position - Index);
}

double FElevatorShaftSpeedProfile::Integral(double Alpha) const
{
	if (Areas.IsEmpty() || !FMath::IsFinite(Alpha)) return 0.0;
	const double Position = FMath::Clamp(Alpha, 0.0, 1.0) * SampleIntervals;
	const int32 Index = FMath::Min(FMath::FloorToInt(Position), SampleIntervals - 1);
	const double Fraction = Position - Index;
	// 마지막 표본 구간도 선형 속도의 면적을 구한다. 누적 면적 자체를 보간하면 속도와 미분이 어긋난다.
	return Areas[Index] + (Speeds[Index] * Fraction
		+ 0.5 * (Speeds[Index + 1] - Speeds[Index]) * Fraction * Fraction) / SampleIntervals;
}

double FElevatorShaftSpeedProfile::FinishBoundary(double Distance, double Height, double Tolerance)
{
	if (!FMath::IsFinite(Distance) || !FMath::IsFinite(Height) || Height <= 0.0
		|| !FMath::IsFinite(Tolerance) || Tolerance < 0.0) return 0.0;
	Distance = FMath::Max(0.0, Distance);
	const double Floors = Distance / Height;
	if (!FMath::IsFinite(Floors)) return 0.0;
	const double NearestBoundary = FMath::RoundToDouble(Floors) * Height;
	const double Boundary = FMath::Abs(Distance - NearestBoundary) <= FMath::Min(Tolerance, Height * 0.5)
		? NearestBoundary : FMath::CeilToDouble(Floors) * Height;
	return FMath::IsFinite(Boundary) ? Boundary : 0.0;
}
