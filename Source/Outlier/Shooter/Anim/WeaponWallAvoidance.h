#pragma once

#include "CoreMinimal.h"
#include "WeaponWallAvoidance.generated.h"

class AActor;
class UWorld;
struct FWeaponValues;

// 각 프레임이 끝난 서버 추정 시각과 입력만 보관한다. 렌더 포즈는 재생 입력에 포함하지 않는다.
struct FWeaponWallPredictionFrame
{
	double ServerTime = 0.0;
	float DeltaSeconds = 0.0f;
	FVector DetectionOrigin = FVector::ZeroVector;
	FRotator AimRotation = FRotator::ZeroRotator;
	bool bAiming = false;
};

// 한 번 갱신한 상태를 판정과 표시가 함께 읽는다. AnimInstance나 렌더 소켓은 입력이 아니다.
USTRUCT()
struct OUTLIER_API FWeaponWallAvoidanceState
{
	GENERATED_BODY()

	UPROPERTY()
	float AimAlpha = 0.0f;
	UPROPERTY()
	float WallOffsetAlpha = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockAlpha = 0.0f;
	UPROPERTY()
	FVector ViewModelWallOffsetLoc = FVector::ZeroVector;
	UPROPERTY()
	FRotator ViewModelWallOffsetRot = FRotator::ZeroRotator;
	UPROPERTY()
	FVector WallOffsetLoc = FVector::ZeroVector;
	UPROPERTY()
	FRotator WallOffsetRot = FRotator::ZeroRotator;
	UPROPERTY()
	float WallAvoidUpAlpha = 0.0f;
	UPROPERTY()
	float WallAvoidDownAlpha = 0.0f;
	UPROPERTY()
	float WallAvoidSideAlpha = 0.0f;
	UPROPERTY()
	float WallAvoidSideSign = 0.0f;
	UPROPERTY()
	float WallTargetAlpha = 0.0f;
	UPROPERTY()
	float WallSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallUpTargetAlpha = 0.0f;
	UPROPERTY()
	float WallUpSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallDownTargetAlpha = 0.0f;
	UPROPERTY()
	float WallDownSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallSideTargetAlpha = 0.0f;
	UPROPERTY()
	float WallSideSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallSideTargetSign = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockTargetAlpha = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallVeryCloseTargetAlpha = 0.0f;
	UPROPERTY()
	float WallVeryCloseOwnTargetAlpha = 0.0f;
	UPROPERTY()
	float WallVeryCloseSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallVeryCloseAlpha = 0.0f;
	UPROPERTY()
	float WallTopEdgeTargetAlpha = 0.0f;
	UPROPERTY()
	float WallTopEdgeAlpha = 0.0f;
	UPROPERTY()
	float WallCeilingTargetAlpha = 0.0f;
	UPROPERTY()
	float WallCeilingAlpha = 0.0f;
	UPROPERTY()
	float WallHardStopTargetAlpha = 0.0f;
	UPROPERTY()
	float WallHardStopOwnTargetAlpha = 0.0f;
	UPROPERTY()
	float WallHardStopSmoothedTargetAlpha = 0.0f;
	UPROPERTY()
	float WallHardStopAlpha = 0.0f;
	UPROPERTY()
	float WallHardStopSafetyAlpha = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockDownPreferenceTargetAlpha = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockDownPreferenceAlpha = 0.0f;
	UPROPERTY()
	float WallMuzzleBlockReleaseHoldTimer = 0.0f;

	void Reset() { *this = FWeaponWallAvoidanceState(); }
	void Update(UWorld* World, const AActor* Shooter, const AActor* Weapon,
		const FVector& DetectionOrigin, const FRotator& AimRotation,
		float DeltaSeconds, const FWeaponValues* WeaponValues, float ActionAlpha = 0.0f);

	// Pose 평가로 얻은 총구와 카메라 기준을 같은 진행도로 연결한다. 조회는 상태를 바꾸지 않는다.
	bool ResolveShotTransform(const FTransform& View, const FTransform& AvoidedMuzzle, FTransform& Out) const;
	float GetShotBlendAlpha() const;
	void GetPoseAlphas(const FWeaponValues& Values, float ActionPoseScale, float& OutUp, float& OutDown) const;
	float GetAimBreakAlpha(const FWeaponValues& Values) const;
	void UpdateAim(bool bAiming, float DeltaSeconds, const FWeaponValues& Values);
};

USTRUCT()
struct OUTLIER_API FWeaponWallAvoidanceSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Weapon = nullptr;
	UPROPERTY()
	uint32 EquipmentRevision = 0;
	UPROPERTY()
	uint32 Sequence = 0;
	UPROPERTY()
	double ServerTime = 0.0;
	UPROPERTY()
	FWeaponWallAvoidanceState State;

	bool MatchesEquipment(const AActor* CurrentWeapon, uint32 CurrentRevision,
		const AActor* InvalidatedWeapon, uint32 InvalidatedRevision) const;

	// 과거 서버 상태를 현재에 덮어쓰지 않고, 그 뒤에 보관한 로컬 입력만 다시 진행한다.
	bool Reconcile(UWorld* World, const AActor* Shooter, const AActor* CurrentWeapon,
		const FWeaponValues& Values, double Now, double LastAppliedServerTime,
		TConstArrayView<FWeaponWallPredictionFrame> Frames, FWeaponWallAvoidanceState& OutState) const;
};
