#pragma once

#include "CoreMinimal.h"
#include "EnemyFlightHeightTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyFlightHeightPreference : uint8
{
	Low,
	Mid,
	High
};

USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyFlightHeightSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height")
	bool bRandomizePreference = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height")
	EEnemyFlightHeightPreference FixedPreference = EEnemyFlightHeightPreference::Mid;

	// 높이는 해당 위치의 바닥에서 Actor 원점까지의 거리(cm)다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0.0", Units = "cm"))
	float LowHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0.0", Units = "cm"))
	float MidHeight = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0.0", Units = "cm"))
	float HighHeight = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0.001", Units = "cm"))
	float FloorProbeDistance = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0.001", Units = "cm"))
	float CorrectionStep = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Flight Height", meta = (ClampMin = "0", ClampMax = "8"))
	int32 CorrectionSteps = 4;

	bool IsValid() const
	{
		return FMath::IsFinite(LowHeight) && FMath::IsFinite(MidHeight) && FMath::IsFinite(HighHeight)
			&& LowHeight >= 0.0f && LowHeight <= MidHeight && MidHeight <= HighHeight
			&& FMath::IsFinite(FloorProbeDistance) && FloorProbeDistance > 0.0f
			&& FMath::IsFinite(CorrectionStep) && CorrectionStep > 0.0f
			&& CorrectionSteps >= 0 && CorrectionSteps <= 8
			&& (FixedPreference == EEnemyFlightHeightPreference::Low
				|| FixedPreference == EEnemyFlightHeightPreference::Mid
				|| FixedPreference == EEnemyFlightHeightPreference::High);
	}

	float GetHeight(EEnemyFlightHeightPreference Preference) const
	{
		switch (Preference)
		{
		case EEnemyFlightHeightPreference::Low:
			return LowHeight;
		case EEnemyFlightHeightPreference::High:
			return HighHeight;
		case EEnemyFlightHeightPreference::Mid:
		default:
			return MidHeight;
		}
	}
};
