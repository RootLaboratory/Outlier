#pragma once

#include "CoreMinimal.h"

class AShooterCharacter;
class AWeaponBase;
struct FWeaponValues;
struct FWeaponWallAvoidanceState;

// 장착 시 애셋의 기준 자세/소켓만 캐시한다. 판정 중에는 표시 메시를 평가하거나 소켓을 읽지 않는다.
struct OUTLIER_API FWeaponWallShotPose
{
	bool Initialize(const AShooterCharacter& Shooter, const AWeaponBase& Weapon);
	bool Evaluate(const FWeaponWallAvoidanceState& State, const FWeaponValues& Values,
		float AimAlpha, const FTransform& View, FTransform& OutMuzzle) const;
	void Reset() { *this = FWeaponWallShotPose(); }
	const FString& GetDiagnostic() const { return Diagnostic; }

private:
	FTransform BaseGun = FTransform::Identity;
	FTransform MuzzleFromHand = FTransform::Identity;
	FTransform MeshToView = FTransform::Identity;
	FString Diagnostic;
	bool bReady = false;
};
