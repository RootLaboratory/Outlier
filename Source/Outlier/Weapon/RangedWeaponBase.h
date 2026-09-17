// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/WeaponRecoilCalculation.h"
#include "Weapon/WeaponRecoilRow.h"
#include "Engine/DataTable.h"
#include "RangedWeaponBase.generated.h"

class UProjectionMarkDefinition;
class UTrailEffectDefinition;
class ULocalPlayerUISubSystem;
class USoundDefinition;
class UWeaponFeedbackDefinition;
class UStaticMesh;
class UStaticMeshComponent;
class AShooterCharacter;
/**
 * 
 */
UCLASS(Abstract)
class OUTLIER_API ARangedWeaponBase : public AWeaponBase
{
	GENERATED_BODY()

public:
	ARangedWeaponBase();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	// 1탄창
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 MagazineSize = 30;

	UPROPERTY(ReplicatedUsing = OnRep_CurAmmo, EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 CurrentAmmo = 30;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	uint8 bInfiniteAmmo : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sight")
	UStaticMesh* SightMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sight")
	UStaticMeshComponent* FirstSight = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sight")
	UStaticMeshComponent* ThirdSight = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sight")
	UStaticMeshComponent* ShadowSight = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Sight")
	FName SightSocketName = FName("Sight");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Magazine")
	UStaticMesh* MagazineMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Magazine")
	UStaticMeshComponent* FirstHandMagazineMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Magazine")
	UStaticMeshComponent* ThirdHandMagazineMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Magazine")
	UStaticMeshComponent* ShadowHandMagazineMesh = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Magazine")
	FName LeftHandMagazineSocketName = FName("Magazine");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cooldown")
	float ReuseCooldown = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Burst", meta = (ClampMin = "0"))
	int32 BurstShotCount = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Burst", meta = (ClampMin = "0.0"))
	float PostBurstCooldownSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	float RecoilMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	FName MuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	FWeaponRecoilRow ActiveRecoilProfile;

	// Bloom : 탄퍼짐

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomCurrent = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomMin = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomMax = 7.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomPerShot = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float BloomRecoveryRate = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Bloom")
	float AimBloomMultiplier = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	uint8 bIsAutomatic : 1 = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	uint8 bIsReloading : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	uint8 bIsAiming : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	uint8 bAttackOnCooldown : 1 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Burst")
	uint8 bOnPostBurstCooldown : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	TObjectPtr<UProjectionMarkDefinition> WeaponDecal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TObjectPtr<UTrailEffectDefinition> WeaponMuzzle; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TObjectPtr<UTrailEffectDefinition> WeaponTrail; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TObjectPtr<USoundDefinition> GunSound; 

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UDataTable> WeaponBloomTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FName BloomProfileId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UDataTable> WeaponProjectileTable;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FName ProjectileProfileId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle ProjectileDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data|Recoil")
	FDataTableRowHandle HipRecoilDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data|Recoil")
	FDataTableRowHandle ADSRecoilDataRow;

	// Hip/ADS 구분이 없는 빙의 드론 등의 무기는 이 Row만 지정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data|Recoil")
	FDataTableRowHandle DefaultRecoilDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UWeaponFeedbackDefinition> FeedbackDefinition;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileSpeedCmPerSec = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileMaxRangeCm = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileStunTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS|Sight")
	FName SightAimScalarParamName = TEXT("Flag");

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SightAimMIDs;

	// 과충전( State.WeaponOvercharged ) 동안 무기 머티리얼에 0 -> 1 로 실어 보낼 스칼라 파라미터.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Overcharge")
	FName OverchargeEmissiveScalarParamName = TEXT("EmissiveStrength");

	// 0 -> 1 ( 및 1 -> 0 ) 도달까지 걸리는 시간. 0 이하면 즉시 반영한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Overcharge")
	float OverchargeEmissiveRampSeconds = 0.35f;

	// 연출 전용이라 복제하지 않는다. 각 머신이 태그 이벤트를 받아 자기 MID 를 직접 굴린다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OverchargeEmissiveMIDs;

	float OverchargeEmissiveAlpha = 0.0f;
	float OverchargeEmissiveTargetAlpha = 0.0f;

	FTimerHandle AutoFireTimerHandle;
	FTimerHandle AttackCooldownTimerHandle;
	FTimerHandle PostBurstCooldownTimerHandle;
	FTimerHandle BloomRecoveryTimerHandle;
	FTimerHandle RecoilResetTimerHandle;
	FTimerHandle LocalControlKickTimerHandle;

	FWeaponRecoilRow CachedHipRecoilProfile;
	FWeaponRecoilRow CachedADSRecoilProfile;
	FWeaponRecoilRow CachedAnyRecoilProfile;
	FWeaponRecoilRuntimeState RecoilRuntimeState;
	FVector2D PendingLocalControlRecoil = FVector2D::ZeroVector;
	FVector2D LastCalculatedControlRecoil = FVector2D::ZeroVector;
	float LocalControlKickInterpSpeed = 100.0f;
	float LocalControlKickElapsedTime = 0.0f;
	float LastWeaponCameraShakeScale = 0.0f;
	float LastWeaponCameraShakeDuration = 0.0f;
	uint32 RecoilShotSequence = 0;
	uint8 bHasCachedHipRecoilProfile : 1 = false;
	uint8 bHasCachedADSRecoilProfile : 1 = false;
	uint8 bHasCachedAnyRecoilProfile : 1 = false;
	uint8 bHasActiveRecoilProfile : 1 = false;

	FVector LastShotBaseDirection = FVector::ForwardVector;
	FVector LastShotDirection = FVector::ForwardVector;
	float LastShotSpreadDegrees = 0.0f;
	int32 CurrentBurstShotCount = 0;
	int32 LastAttackMuzzleShotCount = 1;
	uint8 bHasLastShotDirection : 1 = false;

protected:
	virtual void InitializeFromDataTables() override;

	virtual void InitializeBloomFromDataTable();
	virtual void InitializeRecoilFromDataTable();
	virtual void InitializeProjectileFromDataTable();
	virtual void ApplyFeedbackDefinition();

	void ApplySightMesh();
	void HideSightPresentation();
	void ApplyMagazineMeshSettings();
	void HideHandMagazine();
	void RefreshBloomSettingsFromState();
	void RefreshRecoilSettingsFromState();
	void CacheRecoilProfiles();
	FVector2D GetNormalizedLastShotDirection() const;
	void ApplyLocalRecoilPresentation(
		const FVector2D& NormalizedShotDirection,
		const FVector2D& ControlRecoilDelta,
		float ControlKickInterpSpeed,
		float CameraShakeScale,
		float CameraShakeDuration);
	void ApplyAuthoritativeControlRecoil(const FVector2D& ControlRecoilDelta);
	void QueueLocalControlRecoil(const FVector2D& ControlRecoilDelta, float ControlKickInterpSpeed);
	void HandleLocalControlKick();
	void ApplyControlRotationDelta(const FVector2D& ControlRecoilDelta) const;
	void ResetRecoilRuntimeState();

	void HandleAutoFire();
	float GetAutomaticFireInterval() const;
	float GetEffectiveAttackInterval() const;
	bool IsWeaponOvercharged() const;
	bool HasUsableAmmo() const;
	void StartAttackCooldown();
	void ResetAttackCooldown();
	void StartReuseCooldown();
	void StartPostBurstCooldown();
	void FinishPostBurstCooldown();
	void EnsureBloomRecoveryTimer();
	void HandleBloomRecoveryTimer();
	void CacheSightAimMaterials();
	void CacheOverchargeEmissiveMaterials();
	void ApplyOverchargeEmissiveAlpha();

	// 소유자의 현재 과충전 상태를 보간 없이 그대로 가져온다 ( 장착 / 관련성 복구 시점용 ).
	void RefreshOverchargeEmissiveFromOwner();

	void ReportArenaWideNoise(ACharacter* OwnerCharacter);

public:
	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;
	virtual void OnEquipped(ACharacter* NewOwner) override;
	virtual void OnUnequipped() override;
	virtual void OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy = nullptr) override;
	virtual void ShowEquippedPresentation() override;
	virtual void RefreshShadowWeaponPresentation() override;

	virtual bool CanAttack() const override;
	virtual void StartAttack() override;
	virtual void StopAttack() override;
	virtual void PerformAttack() override;
	bool HasFixedBurst() const { return BurstShotCount > 0; }
	int32 GetCurrentBurstShotCount() const { return CurrentBurstShotCount; }
	bool IsOnPostBurstCooldown() const { return bOnPostBurstCooldown; }
	// 공격이 중간에 취소돼도 정상 버스트와 같은 후딜레이를 적용한다.
	void ForcePostBurstCooldown();

	virtual bool CanReload() const;
	virtual void Reload();
	virtual void BeginReload();
	virtual void FinishReload();
	virtual void CancelReload();
	virtual void ConsumeAmmo();
	virtual void FireShot();
	virtual void ApplyRecoil();
	virtual void ApplyBloomPerShot();
	virtual void RecoverBloom(float DeltaTime);
	virtual float GetCurrentSpread() const;
	int32 GetCurrentAmmo() const { return CurrentAmmo; }
	int32 GetMagazineSize() const { return MagazineSize; }
	void RefillMagazineForWeaponOvercharge();

	// 과충전 연출 : 태그가 서버 / 오너 / 시뮬레이티드 프록시 모두에 복제되므로
	// 별도의 Multicast RPC 없이 각 머신이 자기 1인칭 + 3인칭 MID 를 직접 갱신한다.
	void SetOverchargeEmissiveActive(bool bActive);
	void SnapOverchargeEmissive(float Alpha);
	float GetOverchargeEmissiveAlpha() const { return OverchargeEmissiveAlpha; }

	virtual void SetAiming(bool bAiming);
	void CancelLocalRecoilPresentation();

	virtual void AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner) override;

	void AttachMagazineToLeftHand(AShooterCharacter* Shooter);
	void AttachMagazineToWeapon();
	UStaticMeshComponent* GetFirstSightMesh() const;
	void SetSightAimMaterialFlag(bool bAiming);

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Cooldown")
	bool IsOnReuseCooldown() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Cooldown")
	float GetReuseCooldownRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Cooldown")
	float GetReuseCooldown() const { return ReuseCooldown; }

	UFUNCTION()
	void OnRep_CurAmmo();

	int32 ResolveADSBlurStencil();

protected:
	void UpdateLocalAmmoUI() const;

	// Shooter 는 슈트를 얻기 전까지 MainWidget 이 꺼져 있다. 그 구간에 총을 주워도
	// 탄약 숫자만 따로 올라오면 안 되므로 푸시 자체를 막는다.
	// Partner 무기는 슈트 지급이 유일 경로라 별도 게이트가 필요 없다.
	bool CanPushAmmoUI() const;
	virtual void OnRep_EquippedState() override;

	UFUNCTION(Client, Unreliable)
	void ClientNotifyShotFired(
		FVector2D NormalizedShotDirection,
		FVector2D ControlRecoilDelta,
		float ControlKickInterpSpeed,
		float CameraShakeScale,
		float CameraShakeDuration);

	// 서버에서 확정한 약점 피격 결과를 무기 소유 클라이언트의 크로스헤어에 전달한다.
	UFUNCTION(Client, Unreliable)
	void ClientNotifyAttackSign(bool bCriticalHit);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireFX(FVector_NetQuantize TraceEnd, FVector_NetQuantizeNormal ImpactNormal,
		AActor* Hit, FVector2D NormalizedShotDirection, FName FiredMuzzleSocketName);

	void PlayThirdPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit, FName FiredMuzzleSocketName);

	void PlayFirstPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit, FName FiredMuzzleSocketName);

	void ResolveMuzzleTransforms(bool bFirstPerson, FName FiredMuzzleSocketName,
		TArray<FTransform>& OutMuzzleTransforms) const;
	void FireShotFromMuzzle(FName FiredMuzzleSocketName, bool bPlayShotSound);

	ULocalPlayerUISubSystem* GetLocalUISubsystem() const; //Helper
};
