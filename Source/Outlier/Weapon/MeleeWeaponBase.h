// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/WeaponBase.h"
#include "MeleeWeaponBase.generated.h"

UENUM(BlueprintType)
enum class EMeleeAttackPhase : uint8
{
	Idle,
	Attack,
	Recovery
};

UCLASS(Abstract)
class OUTLIER_API AMeleeWeaponBase : public AWeaponBase
{
	GENERATED_BODY()

public:
	AMeleeWeaponBase();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_AttackPhase, VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	EMeleeAttackPhase AttackPhase = EMeleeAttackPhase::Idle;

	// Animation timing is supplied by the montage in Slice 4. These values drive the interim cycle.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Timing", meta = (ClampMin = "0.01"))
	float AttackDelay = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Timing", meta = (ClampMin = "0.01"))
	float RecoveryDuration = 0.35f;

	int32 AttackSequence = 0;
	bool bWantsToAttack = false;
	FTimerHandle AttackTimerHandle;
	FTimerHandle RecoveryTimerHandle;

	void RefreshOwnerCombatState();

	UFUNCTION()
	void OnRep_AttackPhase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float AttackRadius = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float AttackAngle = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float HitWindow = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float KnockbackMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	bool bCanHitMultipleTargets = false;

public:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanAttack() const override;
	virtual void StartAttack() override;
	void ReleaseAttack();
	virtual void StopAttack() override;
	virtual void PerformAttack() override;

	// Delayed callers must retain the sequence from attack start, not read it at callback time.
	void CommitAttack(int32 ExpectedAttackSequence);
	void FinishAttack(int32 ExpectedAttackSequence);

	UFUNCTION(BlueprintPure, Category = "Weapon|Melee")
	EMeleeAttackPhase GetAttackPhase() const { return AttackPhase; }

	int32 GetAttackSequence() const { return AttackSequence; }

	virtual void TraceMeleeHit();
	virtual void ApplyHitToTarget(AActor* Target);
};
