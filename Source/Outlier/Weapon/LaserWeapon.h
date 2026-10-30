#pragma once

#include "CoreMinimal.h"
#include "Weapon/RangedWeaponBase.h"
#include "LaserWeapon.generated.h"

class UNiagaraComponent;
class ULaserWeaponFeedbackDefinition;

UENUM()
enum class ELaserWeaponPresentationPhase : uint8
{
	Idle,
	Telegraph,
	Firing
};

USTRUCT()
struct FLaserWeaponPresentation
{
	GENERATED_BODY()
	UPROPERTY() ELaserWeaponPresentationPhase Phase = ELaserWeaponPresentationPhase::Idle;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() FVector_NetQuantize EndPoint = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize ShotStart = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;
	UPROPERTY() FName MuzzleSocket = NAME_None;
	UPROPERTY() double ServerStartTime = 0.0;
	UPROPERTY() float Duration = 0.0f;
	UPROPERTY() bool bHasImpact = false;
};

UCLASS(Blueprintable)
class OUTLIER_API ALaserWeapon : public ARangedWeaponBase
{
	GENERATED_BODY()

public:
	ALaserWeapon();
	void BeginCharge(float ChargeDuration, float BeamDuration);
	void EndLaserPresentation();

	virtual void StartAttack() override;
	virtual void PerformAttack() override;
	virtual void StopAttack() override;
	virtual bool CanAttack() const override { return Super::CanAttack() && CanPresentLaser(); }
	virtual bool CanReload() const override { return false; }
	virtual float GetCurrentSpread() const override { return 0.0f; }
	virtual void ApplyRecoil() override {}
	virtual void ApplyBloomPerShot() override {}
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void InitializeFromDataTables() override;
	virtual void OnRep_EquippedState() override;
	virtual void OnHitscanShotResolved(const FVector& ShotStart, const FVector& TraceEnd, const FVector& ImpactNormal,
		bool bHasImpact, FName FiredMuzzleSocketName) override;
	virtual bool ShouldPlayDefaultFireFX() const override { return false; }
	virtual bool ResolveShotTransform(FName FiredMuzzleSocketName, FTransform& OutTransform) const override;

private:
	friend class FOutlierLaserWeaponConfigurationTest;
	friend class FOutlierLaserWeaponPresentationTest;
	void EnforceSingleShotConfiguration();
	bool CanPresentLaser() const;
	bool ResolveLaserMuzzle(FName PreferredSocket, USkeletalMeshComponent*& OutComponent,
		FName& OutSocket, FTransform& OutTransform) const;
	double GetPresentationTime() const;
	void ApplyPresentation(const FLaserWeaponPresentation& State);
	void UpdateLocalPresentation();
	void ClearLocalEffects();
	UNiagaraComponent* CreateEffect(const UTrailEffectDefinition* Definition);
	void UpdateEffect(UNiagaraComponent* Component, const UTrailEffectDefinition* Definition,
		const FTransform& Muzzle, const FVector& EndPoint, float Progress, bool bBeam = true);

	UFUNCTION()
	void OnRep_LaserPresentation();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLaserPresentation(const FLaserWeaponPresentation& State);

	UPROPERTY(ReplicatedUsing = OnRep_LaserPresentation)
	FLaserWeaponPresentation LaserPresentation;
	UPROPERTY(Transient)
	FLaserWeaponPresentation LocalPresentation;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> BeamComponent;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ChargeComponent;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ImpactComponent;

	float ActiveBeamDuration = 0.25f;
	bool bCompletingSingleShot = false;
	bool bHasLocalPresentation = false;
};
