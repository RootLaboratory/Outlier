#pragma once

#include "CoreMinimal.h"
#include "EnemyHackEffectTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyHackEffectEndReason : uint8
{
	Expired,
	ManualRelease,
	TargetDied,
	Cleanup
};

// 만료 시각 하나를 서버 판정과 클라이언트 표시가 함께 사용한다.
USTRUCT()
struct OUTLIER_API FEnemyHackEffectState
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Revision = 0;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	double EndServerTime = 0.0;

	UPROPERTY()
	EEnemyHackEffectEndReason EndReason = EEnemyHackEffectEndReason::Cleanup;
};

// 분/초는 표시용 값이다. 게임플레이 시간은 분리해서 차감하지 않는다.
USTRUCT(BlueprintType)
struct OUTLIER_API FEnemyHackEffectTime
{
	GENERATED_BODY()

	static constexpr float Duration = 60.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Hack")
	int32 Minutes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Hack")
	int32 Seconds = 0;

	static FEnemyHackEffectTime FromRemainingTime(float RemainingTime)
	{
		// 양수인 소수 초를 올려 만료 직전까지 00:01을 유지한다.
		const int32 TotalSeconds = FMath::CeilToInt(FMath::Clamp(RemainingTime, 0.0f, Duration));
		FEnemyHackEffectTime Result;
		Result.Minutes = TotalSeconds / 60;
		Result.Seconds = TotalSeconds % 60;
		return Result;
	}
};
