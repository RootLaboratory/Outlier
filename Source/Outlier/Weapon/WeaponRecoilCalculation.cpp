#include "Weapon/WeaponRecoilCalculation.h"

#include "Weapon/WeaponRecoilRow.h"

FVector2D OutlierWeaponRecoil::CalculateControlRecoil(
	const FWeaponRecoilRow& Profile,
	float WeaponRecoilMultiplier,
	uint32 ShotSequence,
	FWeaponRecoilRuntimeState& RuntimeState)
{
	if (WeaponRecoilMultiplier <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	const float GrowthMultiplier = FMath::Min(
		1.0f + FMath::Max(Profile.RecoilGrowthPerShot, 0.0f) * RuntimeState.ConsecutiveShotCount,
		FMath::Max(Profile.MaxRecoilMultiplier, 1.0f));

	FRandomStream RandomStream(static_cast<int32>(ShotSequence));

	const float Persistence = FMath::Clamp(Profile.YawDirectionPersistence, 0.0f, 1.0f);
	if (RuntimeState.PersistentYawDirection == 0 || RandomStream.FRand() > Persistence)
	{
		RuntimeState.PersistentYawDirection = RandomStream.RandRange(0, 1) == 0 ? -1 : 1;
	}

	const float PitchRandomRange = FMath::Max(Profile.ControlPitchRandomRange, 0.0f);
	const float YawRandomRange = FMath::Max(Profile.ControlYawRandomRange, 0.0f);
	const float PitchMagnitude = FMath::Max(
		(Profile.ControlPitchPerShot * GrowthMultiplier)
			+ RandomStream.FRandRange(-PitchRandomRange, PitchRandomRange),
		0.0f) * WeaponRecoilMultiplier;
	const float YawMagnitude = FMath::Max(
		(Profile.ControlYawPerShot * GrowthMultiplier)
			+ RandomStream.FRandRange(-YawRandomRange, YawRandomRange),
		0.0f) * WeaponRecoilMultiplier;

	const float PreviousPitch = RuntimeState.AccumulatedPitch;
	const float PreviousYaw = RuntimeState.AccumulatedYaw;
	RuntimeState.AccumulatedPitch = FMath::Clamp(
		PreviousPitch + PitchMagnitude,
		0.0f,
		FMath::Clamp(Profile.MaxAccumulatedPitchDegrees, 0.0f, 89.0f));
	RuntimeState.AccumulatedYaw = FMath::Clamp(
		PreviousYaw + (YawMagnitude * RuntimeState.PersistentYawDirection),
		-FMath::Max(Profile.MaxAccumulatedYaw, 0.0f),
		FMath::Max(Profile.MaxAccumulatedYaw, 0.0f));
	++RuntimeState.ConsecutiveShotCount;

	return FVector2D(
		RuntimeState.AccumulatedPitch - PreviousPitch,
		RuntimeState.AccumulatedYaw - PreviousYaw);
}
