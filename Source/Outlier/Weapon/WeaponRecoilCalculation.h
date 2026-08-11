#pragma once

#include "CoreMinimal.h"

struct FWeaponRecoilRow;

/** 연사 구간 안에서만 유지되는 Gameplay 반동 누적 상태다. */
struct FWeaponRecoilRuntimeState
{
	int32 ConsecutiveShotCount = 0;
	float AccumulatedPitch = 0.0f;
	float AccumulatedYaw = 0.0f;
	int8 PersistentYawDirection = 0;

	void Reset()
	{
		ConsecutiveShotCount = 0;
		AccumulatedPitch = 0.0f;
		AccumulatedYaw = 0.0f;
		PersistentYawDirection = 0;
	}
};

namespace OutlierWeaponRecoil
{
	// 발사 순번을 Seed로 사용해 서버에서 재현 가능한 Pitch/Yaw 반동을 계산한다.
	OUTLIER_API FVector2D CalculateControlRecoil(
		const FWeaponRecoilRow& Profile,
		float WeaponRecoilMultiplier,
		uint32 ShotSequence,
		FWeaponRecoilRuntimeState& RuntimeState);
}
