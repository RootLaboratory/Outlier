#pragma once

#include "CoreMinimal.h"

namespace SpinningCircleSweep
{
// 위치는 원 중심을 기준으로 한다. RotationDelta는 이번 프레임의 회전량을
// 정규화하지 않은 도 단위로 나타내므로 긴 프레임에서도 실제 회전 방향을 유지한다.
inline bool HitsWall(const FVector2D& From, const FVector2D& To, double Radius,
	double HalfWidth, double GapSize, double StartAngle, double RotationDelta)
{
	if (GapSize >= 1.0)
	{
		return false;
	}

	const FVector2D Move = To - From;
	const double A = Move.SizeSquared();
	const double B = 2.0 * FVector2D::DotProduct(From, Move);
	const double C = From.SizeSquared();
	const double Inner = FMath::Max(0.0, Radius - HalfWidth);
	const double Outer = Radius + HalfWidth;
	TArray<double, TInlineAllocator<10>> Times;
	Times.Add(0.0);
	Times.Add(1.0);

	auto AddRadiusIntersections = [&](double SquaredRadius)
	{
		if (A <= UE_DOUBLE_SMALL_NUMBER) return;
		const double Discriminant = B * B - 4.0 * A * (C - SquaredRadius);
		if (Discriminant < 0.0) return;
		const double Root = FMath::Sqrt(Discriminant);
		for (double Time : {(-B - Root) / (2.0 * A), (-B + Root) / (2.0 * A)})
		{
			if (Time >= 0.0 && Time <= 1.0) Times.Add(Time);
		}
	};
	AddRadiusIntersections(Inner * Inner);
	AddRadiusIntersections(Outer * Outer);

	// 상대 각도의 극값에서는 cross(From, Move) / radius^2 = omega가 성립한다.
	// 이 지점에서 나누면 각 구간에서 상대 각도가 단조롭게 변한다.
	const double Omega = FMath::DegreesToRadians(RotationDelta);
	const double Cross = From.X * Move.Y - From.Y * Move.X;
	if (!FMath::IsNearlyZero(Omega, UE_DOUBLE_SMALL_NUMBER) && Cross / Omega > 0.0)
	{
		AddRadiusIntersections(Cross / Omega);
	}
	Times.Sort();

	auto RelativeAngle = [&](double Time)
	{
		const FVector2D Point = From + Move * Time;
		const double Angle = FMath::RadiansToDegrees(FMath::Atan2(Point.Y, Point.X));
		const double Relative = FMath::Fmod(Angle - (90.0 + StartAngle + RotationDelta * Time), 360.0);
		return Relative < 0.0 ? Relative + 360.0 : Relative;
	};

	// 접선 및 폭이 0인 원을 포함해 경계 접촉도 검사한다.
	for (double Time : Times)
	{
		const double Distance = (From + Move * Time).Size();
		if (Distance >= Inner - UE_DOUBLE_SMALL_NUMBER && Distance <= Outer + UE_DOUBLE_SMALL_NUMBER
			&& (GapSize <= 0.0 || RelativeAngle(Time) >= GapSize * 360.0))
		{
			return true;
		}
	}

	for (int32 Index = 1; Index < Times.Num(); ++Index)
	{
		const double Begin = Times[Index - 1];
		const double End = Times[Index];
		const double Distance = (From + Move * ((Begin + End) * 0.5)).Size();
		if (Distance < Inner || Distance > Outer) continue;

		const FVector2D P = From + Move * Begin;
		const FVector2D Q = From + Move * End;
		const double AngleChange = FMath::RadiansToDegrees(FMath::Atan2(
			P.X * Q.Y - P.Y * Q.X, FVector2D::DotProduct(P, Q)));
		const double RelativeBegin = RelativeAngle(Begin);
		const double RelativeEnd = RelativeBegin + AngleChange - RotationDelta * (End - Begin);
		if (GapSize <= 0.0 || FMath::Min(RelativeBegin, RelativeEnd) < 0.0
			|| FMath::Max(RelativeBegin, RelativeEnd) >= GapSize * 360.0)
		{
			return true;
		}
	}
	return false;
}
}
