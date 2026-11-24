#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "OutlierPlayerState.h"
#include "PairPositionMarkerTypes.generated.h"

class AFirstPersonCharacter;

USTRUCT()
struct FPairPositionMarkerSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize Position = FVector::ZeroVector;
	UPROPERTY()
	double SampleServerTime = 0.0;
	UPROPERTY()
	int32 PairId = INDEX_NONE;
	UPROPERTY()
	EOutlierPlayerRole TargetRole = EOutlierPlayerRole::None;
	UPROPERTY()
	uint32 GameplayGeneration = 0;
	UPROPERTY()
	uint32 TargetGeneration = 0;

	bool IsFor(int32 ViewerPairId, EOutlierPlayerRole ViewerRole, uint32 CurrentGameplayGeneration) const
	{
		return PairId != INDEX_NONE && PairId == ViewerPairId && TargetGeneration != 0
			&& GameplayGeneration == CurrentGameplayGeneration
			&& ((ViewerRole == EOutlierPlayerRole::Shooter && TargetRole == EOutlierPlayerRole::Partner)
				|| (ViewerRole == EOutlierPlayerRole::Partner && TargetRole == EOutlierPlayerRole::Shooter));
	}

	bool HasFreshPosition(double ServerTime) const
	{
		return ServerTime - SampleServerTime < 2.0;
	}
};

// Actor 참조 해석이 늦어져도 보조 좌표 스냅샷은 독립적으로 적용한다.
USTRUCT()
struct FPairPositionMarkerTarget
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AFirstPersonCharacter> Pawn;
	UPROPERTY()
	uint32 TargetGeneration = 0;

	bool Matches(const FPairPositionMarkerSnapshot& Snapshot) const
	{
		return TargetGeneration != 0 && TargetGeneration == Snapshot.TargetGeneration;
	}
};

USTRUCT()
struct FPairPositionMarkerHit
{
	GENERATED_BODY()

	UPROPERTY()
	double HitServerTime = 0.0;
	UPROPERTY()
	int32 PairId = INDEX_NONE;
	UPROPERTY()
	EOutlierPlayerRole TargetRole = EOutlierPlayerRole::None;
	UPROPERTY()
	uint32 GameplayGeneration = 0;
	UPROPERTY()
	uint32 TargetGeneration = 0;
	UPROPERTY()
	uint32 Sequence = 0;

	bool Matches(const FPairPositionMarkerSnapshot& Snapshot) const
	{
		return PairId == Snapshot.PairId && TargetRole == Snapshot.TargetRole
			&& GameplayGeneration == Snapshot.GameplayGeneration && TargetGeneration == Snapshot.TargetGeneration;
	}

	double GetRemainingSeconds(double ServerTime, double Duration) const
	{
		return FMath::Clamp(HitServerTime + Duration - ServerTime, 0.0, Duration);
	}
};
