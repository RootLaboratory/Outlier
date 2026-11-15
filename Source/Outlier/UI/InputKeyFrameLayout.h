#pragma once

#include "CoreMinimal.h"

/** The two key-frame textures share a 154px source and an 8px rounded corner. */
struct FInputKeyFrameLayout
{
	static constexpr float SourceSize = 154.0f;
	static constexpr float SourceCorner = 8.0f;
	static constexpr float SourceStroke = 4.0f;
	static constexpr float SourceMargin = SourceCorner / SourceSize;

	static FVector2D GetCornerSize(const FVector2D& Size)
	{
		const double Corner = FMath::Max(0.0, Size.Y) * SourceMargin;
		return FVector2D(FMath::Min(Corner, FMath::Max(0.0, Size.X) * 0.5),
			FMath::Min(Corner, FMath::Max(0.0, Size.Y) * 0.5));
	}

	/** Quantize stroke thickness in render-target pixels, not widget/layout units. */
	static FVector2D GetPixelAlignedCornerSize(const FVector2D& Size, const FVector2D& PixelScale)
	{
		const FVector2D Natural = GetCornerSize(Size);
		const double StrokeRatio = SourceStroke / SourceCorner;
		auto Corner = [StrokeRatio](double NaturalCorner, double Scale, double Extent)
		{
			Scale = FMath::Max(Scale, UE_DOUBLE_SMALL_NUMBER);
			const double StrokePixels = FMath::Max(1.0, FMath::RoundToDouble(NaturalCorner * Scale * StrokeRatio));
			return FMath::Min(StrokePixels / (StrokeRatio * Scale), Extent * 0.5);
		};
		return FVector2D(Corner(Natural.X, PixelScale.X, Size.X), Corner(Natural.Y, PixelScale.Y, Size.Y));
	}
};
