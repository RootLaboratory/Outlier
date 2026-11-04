#pragma once

#include "CoreMinimal.h"

// M_Spherical의 RetainerBox 투영에 맞춰 출력 화면 UV를 원본 UV로 변환한다.
inline FVector2D ShieldScreenToSourceUV(const FVector2D& ScreenUV,
	float HorizontalArc, float VerticalArc, float SourceHFov, float SourceVFov,
	float WarpAmount, float TextureScale)
{
	const double HalfWidth = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(SourceHFov, 1.f, 170.f)) * 0.5);
	const double HalfHeight = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(SourceVFov, 1.f, 170.f)) * 0.5);
	const FVector Ray(1.0, (ScreenUV.X * 2.0 - 1.0) * HalfWidth, -(ScreenUV.Y * 2.0 - 1.0) * HalfHeight);
	const FVector2D AngularUV(
		0.5 + FMath::Atan2(Ray.Y, Ray.X) / FMath::DegreesToRadians(FMath::Clamp(HorizontalArc, 1.f, 175.f)),
		0.5 - FMath::Atan2(Ray.Z, FMath::Sqrt(Ray.X * Ray.X + Ray.Y * Ray.Y)) / FMath::DegreesToRadians(FMath::Clamp(VerticalArc, 1.f, 175.f)));
	return (FMath::Lerp(ScreenUV, AngularUV, FMath::Clamp(WarpAmount, 0.f, 1.f)) - FVector2D(0.5))
		/ FMath::Max(TextureScale, 0.001f) + FVector2D(0.5);
}
