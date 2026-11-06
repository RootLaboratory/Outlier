#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
class ARangedWeaponBase;
class ACharacter;
struct FWeaponShotSnapshot;

// 발사 결과를 읽기만 한다. 제품 RPC와 판정에는 테스트 식별값을 섞지 않는다.
struct FWallShotObservation
{
	ARangedWeaponBase* Weapon = nullptr;
	bool bPresentation = false;
	FVector Start = FVector::ZeroVector;
	FVector BaseDirection = FVector::ForwardVector;
	FVector Direction = FVector::ForwardVector;
	FVector End = FVector::ZeroVector;
	FName Socket = NAME_None;
	bool bHit = false;
	FString HitActor;
	FString HitComponent;
	bool bStartPenetrating = false;
	uint32 ShotId = 0;
	uint32 EquipmentRevision = 0;
	float ServerTime = 0.0f;
	bool bOccluded = false;
	bool bWorldEffects = false;
};

namespace WallShotProbe
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FObserved, const FWallShotObservation&);
	FObserved& Observed();
}

// 실제 BP 무기를 그대로 사용하면서 protected 사격 경로를 검사하는 테스트 전용 접근점.
struct FWallShotProbeAccess
{
	static bool Resolve(ARangedWeaponBase* Weapon, FTransform& Out);
	static bool ResolveWall(ARangedWeaponBase* Weapon, ACharacter* Owner,
		const FVector& View, const FRotator& Rotation, FVector& Start, FVector& Direction);
	static void ConfigureControlledShot(ARangedWeaponBase* Weapon);
	static void AttackOnce(ARangedWeaponBase* Weapon);
	static FWeaponShotSnapshot GetLastShot(const ARangedWeaponBase* Weapon);
	static void Present(ARangedWeaponBase* Weapon, const FWeaponShotSnapshot& Shot);
	static bool Occlusion(ARangedWeaponBase* Weapon, const FVector& View, const FVector& Start, FHitResult& Hit);
	static bool Trace(ARangedWeaponBase* Weapon, const FVector& Start, const FVector& End, FHitResult& Hit);
	static void ConfigureShotCosts(ARangedWeaponBase* Weapon);
	static uint32 GetRecoilSequence(const ARangedWeaponBase* Weapon);
	static void SetAttackGate(ARangedWeaponBase* Weapon, int32 Ammo, bool bReloading);
};
#endif
