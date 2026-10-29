// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interface/MeleeTargetInterface.h"
#include "Weapon/WeaponBase.h"
#include "MeleeWeaponBase.generated.h"

class UAnimMontage;

struct FOutlierStealthSession;

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

	// 서버에서 재생할 유효한 3인칭 공격 몽타주가 없으면 이 값을 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Timing", meta = (ClampMin = "0.01"))
	float AttackDelay = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Timing", meta = (ClampMin = "0.01"))
	float RecoveryDuration = 0.35f;

	int32 AttackSequence = 0;
	bool bPlayedMeleeHitSoundThisAttack = false;
	bool bWantsToAttack = false;
	FTimerHandle AttackTimerHandle;
	FTimerHandle RecoveryTimerHandle;
	FTimerHandle TargetSearchTimerHandle;
	bool bUsesAnimationTiming = false;
	bool bMeleeTraceActive = false;
	int32 ActiveMeleeTraceSequence = 0;
	FVector PreviousMeleeTraceStart = FVector::ZeroVector;
	FVector PreviousMeleeTraceEnd = FVector::ZeroVector;
	TSet<AActor*> HitActorsThisSwing;
	TSharedPtr<FOutlierStealthSession> SwingStealthSession;
	TArray<FHitResult> MeleeTraceHitBuffer;
	float CurrentTargetSearchInterval = 0.1f;
	TWeakObjectPtr<AActor> CurrentMeleeTarget;

	void RefreshOwnerCombatState();
	bool PlayAttackAnimation();
	void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 ExpectedAttackSequence);
	void RefreshTargetSearchState();
	void ScheduleTargetSearch(float Delay);
	void RefreshMeleeTarget();
	void StopTargetSearch();
	void SetCurrentMeleeTarget(AActor* NewTarget);
	bool FindBestMeleeTarget(FHitResult& OutHit, bool bIncludePartner, bool bRequireIndicatorEligibility) const;
	bool IsValidMeleeTarget(AActor* Candidate, bool bIncludePartner, bool bRequireIndicatorEligibility) const;
	virtual bool GetMeleeTraceSocketLocations(FVector& OutStart, FVector& OutEnd) const;
	void SweepMeleeTrace(
		const FVector& PreviousStart,
		const FVector& PreviousEnd,
		const FVector& CurrentStart,
		const FVector& CurrentEnd);
	void ResetMeleeTraceState();
	void PlayMeleeHitSoundOnce(const FHitResult& HitResult, uint8 Surface);
	void LogMissingMeleeTraceSockets() const;
	virtual void NotifyMeleeHitResult(const FMeleeHitContext& Context);

	UFUNCTION()
	void OnRep_AttackPhase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Target Search", meta = (ClampMin = "0.0"))
	float TargetSearchRange = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Target Search", meta = (ClampMin = "0.0"))
	float TargetSearchRadius = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Trace")
	FName MeleeTraceStartSocketName = TEXT("MeleeTraceStart");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Trace")
	FName MeleeTraceEndSocketName = TEXT("MeleeTraceEnd");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Trace", meta = (ClampMin = "0.0"))
	float TraceRadius = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Debug")
	bool bDrawDebugMeleeTrace = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Debug", meta = (ClampMin = "0.0"))
	float DebugMeleeTraceDuration = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float AttackAngle = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float HitWindow = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	float KnockbackMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	bool bCanHitMultipleTargets = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Target Search", meta = (ClampMin = "0.01"))
	float InitialTargetSearchInterval = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Target Search", meta = (ClampMin = "0.0"))
	float TargetSearchIntervalStep = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee|Target Search", meta = (ClampMin = "0.01"))
	float MaxTargetSearchInterval = 0.25f;

	UFUNCTION(Client, Reliable)
	void ClientNotifyMeleeHitFeedback(const FMeleeHitContext& Context);

	void PresentSurfaceImpact(const FHitResult& Hit);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySurfaceImpact(FVector_NetQuantize Location, FVector_NetQuantizeNormal Normal,
		AActor* HitActor, uint8 Surface, FGameplayTag Context);

protected:
	virtual void OnRep_EquippedState() override;

public:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnEquipped(ACharacter* NewOwner) override;
	virtual void OnUnequipped() override;
	virtual void OnDropped(const FTransform& DropTransform, AFirstPersonCharacter* DroppedBy = nullptr) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanAttack() const override;
	virtual void StartAttack() override;
	void ReleaseAttack();
	virtual void StopAttack() override;
	virtual void PerformAttack() override;

	// 지연 호출에는 공격 시작 시 저장한 시퀀스를 전달한다. 콜백 시점의 현재 시퀀스를 사용하면 안 된다.
	void CommitAttack(int32 ExpectedAttackSequence);
	void FinishAttack(int32 ExpectedAttackSequence);
	void HandleHitNotify();
	void HandleRecoveryEndNotify();
	bool BeginMeleeTrace();
	void TickMeleeTrace();
	void EndMeleeTrace();

	UFUNCTION(BlueprintPure, Category = "Weapon|Melee")
	EMeleeAttackPhase GetAttackPhase() const { return AttackPhase; }

	int32 GetAttackSequence() const { return AttackSequence; }
	AActor* GetCurrentMeleeTarget() const { return CurrentMeleeTarget.Get(); }

	virtual void TraceMeleeHit();
	virtual void ApplyHitToTarget(AActor* Target);
	virtual void ApplyHitToTarget(AActor* Target, const FHitResult& HitResult);

	static float CalculateNextTargetSearchInterval(
		float CurrentInterval,
		bool bHasTarget,
		bool bTargetChanged,
		float InitialInterval,
		float IntervalStep,
		float MaxInterval);
};
