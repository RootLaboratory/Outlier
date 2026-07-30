// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Weapon/WeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "ShooterAnimInstance.generated.h"

struct FWeaponValues;

/**
 * 
 */
UCLASS()
class OUTLIER_API UShooterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Anim|TP Procedural|Recoil")
	void AddThirdPersonRecoil(float GameplayRecoilScale, FVector2D NormalizedShotDirection);

	// 벽 ADS 해제의 단일 기준값. 1인칭이 이 값을 읽어 같은 프레임에 같은 값으로
	// 조준을 해제한다 (양쪽이 따로 계산하면 해제 시점이 어긋남)
	float GetWallAimBreakAlpha() const { return ThirdPersonWallAimBreakAlpha; }

protected:
	UFUNCTION()
	void HandleOwnerDeath();

	UFUNCTION()
	void HandleOwnerMovementStateChanged(EMovementState NewState);

	void ResetThirdPersonProceduralState();
	void UpdateThirdPersonProceduralState(float DeltaSeconds, AWeaponBase* CurrentWeapon);
	void UpdateThirdPersonLean(float DeltaSeconds);
	float GetThirdPersonSprintPoseTargetAlpha(const FWeaponValues* WeaponValues) const;
	void UpdateThirdPersonRecoil(float DeltaSeconds, const FWeaponValues* WeaponValues);
	void UpdateThirdPersonWallOffset(float DeltaSeconds, AWeaponBase* CurrentWeapon, const FWeaponValues* WeaponValues);
	float UpdateTurnInPlaceYaw(float DeltaSeconds, bool bCanTurnInPlace);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Speed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float Direction = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	EWeaponType CurrentWeaponType = EWeaponType::Unarmed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimYaw = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	float AimPitch = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	float LeanAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSprinting : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSliding : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsDead : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	EMovementState MovementState = EMovementState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	ECombatState CombatState = ECombatState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	EWeaponMode WeaponMode = EWeaponMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsCrouching : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsSlidingCanceled : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stat)
	uint8 bIsGrounded : 1 = true;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsInAir : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|Movement")
	float VelocityZ = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsPrimaryWeapon : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim")
	uint8 bIsSecondaryWeapon : 1 = false;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> CachedShooterCharacter = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurnInPlace")
	float TurnInPlaceVisualYaw = 0.0f;

	float PreviousBaseAimYaw = 0.0f;
	float TurnInPlaceResetTimeRemaining = 0.0f;
	uint8 bHasPreviousBaseAimYaw : 1 = false;
	uint8 bIsTurnInPlaceConsuming : 1 = false;
	uint8 bTurnInPlaceResetRequested : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonADSAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonSprintPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonLeanLeftAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonLeanRightAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonADSLeanLeftAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonADSLeanRightAlpha = 0.0f;

	// 벽 회피 포즈 통합 알파 (내부용). ABP에는 아래의 Hip/ADS 분할본이 바인딩된다
	float ThirdPersonWallAvoidUpPoseAlpha = 0.0f;
	float ThirdPersonWallAvoidDownPoseAlpha = 0.0f;

	// 벽 근접에 의한 ADS 해제 정도(0=유지, 1=완전 해제). 1인칭도 이 값을 공유
	float ThirdPersonWallAimBreakAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallTightAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallTightADSAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallAvoidUpHipPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallAvoidUpADSPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallAvoidDownHipPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Additive Alpha")
	float ThirdPersonWallAvoidDownADSPoseAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Aim")
	float ThirdPersonAimAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Aim")
	float ThirdPersonAimOffsetAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Aim")
	float ThirdPersonHipAimOffsetAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Aim")
	float ThirdPersonADSAimOffsetAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Upper Body")
	float ThirdPersonUpperBodyAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Action")
	float ThirdPersonFireAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Action")
	float ThirdPersonReloadAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Action")
	float ThirdPersonSlideAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Turn In Place")
	float ThirdPersonTurnInPlaceAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Turn In Place")
	float ThirdPersonTurnInPlacePlayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Recoil")
	FVector ThirdPersonRecoilLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Recoil")
	FRotator ThirdPersonRecoilRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Recoil")
	float ThirdPersonRecoilAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Recoil")
	FVector ThirdPersonRecoilSpineLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Recoil")
	FRotator ThirdPersonRecoilSpineRot = FRotator::ZeroRotator;

	// ABP의 hand_r/hand_l ModifyBone(Component Space, Additive)에 바인딩되는
	// 손 당김 최종 출력
	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Wall Offset")
	FVector ThirdPersonWallOffsetLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Anim|TP Procedural|Wall Offset")
	FRotator ThirdPersonWallOffsetRot = FRotator::ZeroRotator;

	// 벽 대응 단계별 보간 알파 (내부용, ABP 바인딩 없음)
	float ThirdPersonWallOffsetAlpha = 0.0f;      // 기본 당김(center 프로브)
	float ThirdPersonWallMuzzleBlockAlpha = 0.0f; // 총구/총열 차단
	float ThirdPersonWallVeryCloseAlpha = 0.0f;   // 근접
	float ThirdPersonWallHardStopAlpha = 0.0f;    // 최근접(WallTight 포즈 구동)

	// 최근 프로브 히트에서 얻은 수평화된 월드 벽 노멀. 손 당김의 방향/세기
	// 판정에 사용하며, 릴리즈 후에도 유지해 블렌드 아웃 방향이 튀지 않게 한다
	FVector ThirdPersonWallPullDirWorld = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector ThirdPersonRecoilLocTarget = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator ThirdPersonRecoilRotTarget = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Aim", meta = (ClampMin = "0.0"))
	float ThirdPersonAimBlendInSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Aim", meta = (ClampMin = "0.0"))
	float ThirdPersonAimBlendOutSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Upper Body", meta = (ClampMin = "0.0"))
	float ThirdPersonUpperBodyBlendInSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Upper Body", meta = (ClampMin = "0.0"))
	float ThirdPersonUpperBodyBlendOutSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMinSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceStartYaw = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceFullYaw = 115.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceYawConsumeSpeed = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceResetHoldTime = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceBlendInSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceBlendOutSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMinPlayRate = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Turn In Place", meta = (ClampMin = "0.0"))
	float ThirdPersonTurnInPlaceMaxPlayRate = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Action", meta = (ClampMin = "0.0"))
	float ThirdPersonActionBlendInSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Action", meta = (ClampMin = "0.0"))
	float ThirdPersonActionBlendOutSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Lean", meta = (ClampMin = "0.0"))
	float ThirdPersonLeanInterpSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil")
	FVector ThirdPersonRecoilSpineLocScale = FVector(-0.25f, 0.0f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil")
	FRotator ThirdPersonRecoilSpineRotScale = FRotator(-1.8f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil")
	FVector ThirdPersonRecoilHandLocScale = FVector(-1.4f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil")
	FRotator ThirdPersonRecoilHandRotScale = FRotator(-3.0f, 0.6f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil", meta = (ClampMin = "0.0"))
	float ThirdPersonRecoilImpulseScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Recoil", meta = (ClampMin = "0.0"))
	float ThirdPersonRecoilRecoverySpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallTraceDistance = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallTraceForwardOffset = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset")
	float ThirdPersonWallTraceHeightOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallSafeDistance = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallTraceRadius = 8.0f;

	// 히트 노멀 Z가 이 값보다 크면 바닥/완만한 경사로 보고 벽 후보에서 제외.
	// 0.6 ≈ 53도보다 완만한 면은 무시 (앉아서 아래를 볼 때 바닥 오발동 방지)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallFloorNormalZ = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallVerticalProbeOffset = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallSideProbeOffset = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallMuzzleBlockTraceDistance = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallMuzzleBlockSafeDistance = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|MuzzleBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallMuzzleBlockTraceRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|BarrelBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallBarrelBlockLength = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|BarrelBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallBarrelBlockSafeDistance = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|BarrelBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallBarrelBlockTraceRadius = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|BarrelBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallPitchBlockTraceRadiusBoost = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|BarrelBlock", meta = (ClampMin = "0.0"))
	float ThirdPersonWallPitchBlockLengthBoost = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallAimOffsetSuppressScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallAimBreakStartAlpha = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallAimBreakFullAlpha = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallVeryCloseStartAlpha = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|VeryClose", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallVeryCloseFullAlpha = 1.0f;

	// 손 당김 튜닝값: X = 조준 반대 방향 당김(음수), Z = 아래 방향. Y는 사용 안 함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|VeryClose")
	FVector ThirdPersonWallVeryCloseHandLoc = FVector(-8.0f, 0.0f, -2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|HardStop", meta = (ClampMin = "0.0"))
	float ThirdPersonWallHardStopClearance = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|HardStop", meta = (ClampMin = "0.0"))
	float ThirdPersonWallHardStopRange = 25.0f;

	// 실제 히트 거리 기반이라 프로브 알파가 포화된 뒤에도 근접 정보가 남는다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|HardStop")
	FVector ThirdPersonWallHardStopHandLoc = FVector(-14.0f, 0.0f, -4.0f);

	// 상단 경계(위 프로브만 벽을 보는 경우)에서 근접 단계로 넘기는 비율
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|Ceiling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallCeilingVeryCloseScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|Ceiling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallCeilingHardStopScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset|MuzzleBlock", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallMuzzleBlockAvoidUpScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThirdPersonWallPoseSuppressScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallBlendInSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset", meta = (ClampMin = "0.0"))
	float ThirdPersonWallBlendOutSpeed = 10.0f;

	// 기본 손 당김 튜닝값 (WeaponValues가 있으면 그쪽 값이 우선).
	// X = 조준 반대 방향 당김(음수), Z = 아래 방향. Y는 사용 안 함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset")
	FVector ThirdPersonWallWeaponHandLoc = FVector(-18.0f, 0.0f, -4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim|TP Procedural|Wall Offset")
	FRotator ThirdPersonWallWeaponHandRot = FRotator(-10.0f, 0.0f, 0.0f);

};
