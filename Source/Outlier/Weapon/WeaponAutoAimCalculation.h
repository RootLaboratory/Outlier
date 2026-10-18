#pragma once

#include "CoreMinimal.h"

namespace OutlierAutoAim
{
	/** 카메라 로컬 위치를 화면 가로폭 기준으로 투영하고, 화면과 조준 원 안에 있는지 검사한다. */
	inline bool TryGetScreenDistanceSquared(
		const FVector& CameraLocalPoint,
		float HorizontalFOVDegrees,
		float AspectRatio,
		float DiameterWidthRatio,
		float& OutDistanceSquared)
	{
		OutDistanceSquared = 0.0f;
		if (!FMath::IsFinite(CameraLocalPoint.X)
			|| !FMath::IsFinite(CameraLocalPoint.Y)
			|| !FMath::IsFinite(CameraLocalPoint.Z)
			|| CameraLocalPoint.X <= 0.0
			|| !FMath::IsFinite(HorizontalFOVDegrees)
			|| HorizontalFOVDegrees <= 0.0f || HorizontalFOVDegrees >= 180.0f
			|| !FMath::IsFinite(AspectRatio) || AspectRatio <= 0.0f
			|| !FMath::IsFinite(DiameterWidthRatio)
			|| DiameterWidthRatio <= 0.0f || DiameterWidthRatio > 1.0f)
		{
			return false;
		}

		// X는 전방 거리다. 가로/세로 모두 화면 가로폭으로 나눠야 UI와 같은 원형 거리가 된다.
		const double ProjectionWidth = 2.0 * CameraLocalPoint.X
			* FMath::Tan(FMath::DegreesToRadians(static_cast<double>(HorizontalFOVDegrees)) * 0.5);
		if (!FMath::IsFinite(ProjectionWidth) || ProjectionWidth <= 0.0)
		{
			return false;
		}

		const double ScreenX = CameraLocalPoint.Y / ProjectionWidth;
		const double ScreenY = CameraLocalPoint.Z / ProjectionWidth;
		if (!FMath::IsFinite(ScreenX) || !FMath::IsFinite(ScreenY)
			|| FMath::Abs(ScreenX) > 0.5 || FMath::Abs(ScreenY) > 0.5 / AspectRatio)
		{
			return false;
		}

		// 초광폭 화면에서는 원의 위아래가 화면 밖으로 나갈 수 있으므로 화면 경계를 먼저 검사한다.
		const double DistanceSquared = FMath::Square(ScreenX) + FMath::Square(ScreenY);
		const double Radius = static_cast<double>(DiameterWidthRatio) * 0.5;
		if (DistanceSquared > FMath::Square(Radius))
		{
			return false;
		}

		OutDistanceSquared = static_cast<float>(DistanceSquared);
		return true;
	}
}
