// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponBase.h"
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	float RecoilMultiplier = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	float RecoilPitchAmplitude = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	float RecoilLocationXAmplitude = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	float RecoilLocationYAmplitude = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	float RecoilFovAmplitude = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	float RecoilRecoverySpeed = 0.0f;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Cooldown")
	uint8 bOnReuseCooldown : 1 = false;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UDataTable> WeaponRecoilTable;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FName RecoilProfileId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	FDataTableRowHandle RecoilDataRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Data")
	TObjectPtr<UWeaponFeedbackDefinition> FeedbackDefinition;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileSpeedCmPerSec = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileMaxRangeCm = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	float ProjectileStunTime = 0.0f;

	FTimerHandle AutoFireTimerHandle;
	FTimerHandle AttackCooldownTimerHandle;
	FTimerHandle ReuseCooldownTimerHandle;

	FVector LastShotBaseDirection = FVector::ForwardVector;
	FVector LastShotDirection = FVector::ForwardVector;
	float LastShotSpreadDegrees = 0.0f;
	uint8 bHasLastShotDirection : 1 = false;

protected:
	virtual void InitializeFromDataTables() override;

	virtual void InitializeBloomFromDataTable();
	virtual void InitializeRecoilFromDataTable();
	virtual void InitializeProjectileFromDataTable();
	virtual void ApplyFeedbackDefinition();

	void ApplySightMesh();
	void ApplyMagazineMeshSettings();
	void HideHandMagazine();
	void RefreshBloomSettingsFromState();
	void RefreshRecoilSettingsFromState();
	FVector2D GetNormalizedLastShotDirection() const;
	void ApplyRecoilWithShotDirection(const FVector2D& NormalizedShotDirection);

	void HandleAutoFire();
	void StartAttackCooldown();
	void ResetAttackCooldown();
	void StartReuseCooldown();
	void FinishReuseCooldown();

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;
	virtual void OnEquipped(ACharacter* NewOwner) override;
	virtual void ShowEquippedPresentation() override;
	virtual void RefreshShadowWeaponPresentation() override;

	virtual bool CanAttack() const override;
	virtual void StartAttack() override;
	virtual void StopAttack() override;
	virtual void PerformAttack() override;

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

	virtual void SetAiming(bool bAiming);

	virtual void AttachWeaponMeshesToOwner(AWeaponBase* Weapon, ACharacter* NewOwner) override;

	void AttachMagazineToLeftHand(AShooterCharacter* Shooter);
	void AttachMagazineToWeapon();

	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Cooldown")
	bool IsOnReuseCooldown() const { return bOnReuseCooldown; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Cooldown")
	float GetReuseCooldown() const { return ReuseCooldown; }

	UFUNCTION()
	void OnRep_CurAmmo();

protected:
	void UpdateLocalAmmoUI() const;
	virtual void OnRep_EquippedState() override;

	UFUNCTION(Client, Unreliable)
	void ClientNotifyShotFired(FVector2D NormalizedShotDirection);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireFX(FVector_NetQuantize TraceEnd, FVector_NetQuantizeNormal ImpactNormal, AActor* Hit);

	void PlayThirdPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit);

	void PlayFirstPersonFireFX(FVector TraceEnd, FVector ImpactNormal, AActor* Hit);

	ULocalPlayerUISubSystem* GetLocalUISubsystem() const; //Helper
};
