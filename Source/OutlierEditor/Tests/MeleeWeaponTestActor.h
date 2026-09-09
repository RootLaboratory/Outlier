#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "Interface/MeleeTargetInterface.h"
#include "Weapon/MeleeWeaponBase.h"
#include "TimerManager.h"
#include "MeleeWeaponTestActor.generated.h"

// Concrete editor-only fixture for the abstract gameplay weapon lifecycle tests.
UCLASS(Transient, NotBlueprintable)
class AMeleeWeaponTestActor : public AMeleeWeaponBase
{
	GENERATED_BODY()

public:
	void SetTestOwner(ACharacter* InOwner)
	{
		WeaponOwner = InOwner;
		bIsEquipped = InOwner != nullptr;
		SetOwner(InOwner);
	}

	bool HasPendingAttackTimers() const
	{
		return GetWorldTimerManager().TimerExists(AttackTimerHandle)
			|| GetWorldTimerManager().TimerExists(RecoveryTimerHandle);
	}

	void SetTestTraceConfig(float InAttackRange, float InAttackRadius)
	{
		AttackRange = InAttackRange;
		AttackRadius = InAttackRadius;
	}

	void ResetAppliedTarget()
	{
		LastAppliedTarget.Reset();
		AppliedTargetCount = 0;
	}

	void ResetHitResults()
	{
		LastHitContext = FMeleeHitContext();
		HitResultCount = 0;
	}

	AActor* GetLastAppliedTarget() const { return LastAppliedTarget.Get(); }
	int32 GetAppliedTargetCount() const { return AppliedTargetCount; }
	int32 GetHitResultCount() const { return HitResultCount; }
	const FMeleeHitContext& GetLastHitContext() const { return LastHitContext; }
	void SetTestDamage(float InDamage) { Damage = InDamage; }
	void ApplyDamageToTargetForTest(AActor* Target)
	{
		AMeleeWeaponBase::ApplyHitToTarget(Target, FHitResult());
	}

	AActor* FindIndicatorTargetForTest() const
	{
		FHitResult HitResult;
		return FindBestMeleeTarget(HitResult, false, true) ? HitResult.GetActor() : nullptr;
	}

	void SetCurrentMeleeTargetForTest(AActor* Target) { SetCurrentMeleeTarget(Target); }
	void NotifyMeleeHitResultForTest(const FMeleeHitContext& Context) { AMeleeWeaponBase::NotifyMeleeHitResult(Context); }
	bool HasPendingTargetSearch() const { return GetWorldTimerManager().TimerExists(TargetSearchTimerHandle); }

	virtual void ApplyHitToTarget(AActor* Target, const FHitResult& /*HitResult*/) override
	{
		LastAppliedTarget = Target;
		++AppliedTargetCount;
	}

	virtual void NotifyMeleeHitResult(const FMeleeHitContext& Context) override
	{
		LastHitContext = Context;
		++HitResultCount;
		Super::NotifyMeleeHitResult(Context);
	}

private:
	TWeakObjectPtr<AActor> LastAppliedTarget;
	int32 AppliedTargetCount = 0;
	FMeleeHitContext LastHitContext;
	int32 HitResultCount = 0;
};

UCLASS(Transient, NotBlueprintable)
class AMeleeTargetTestEnemy : public AEnemyBase
{
	GENERATED_BODY()

public:
	virtual bool CanShowMeleeTargetIndicator_Implementation(const AActor* InstigatorActor) const override
	{
		return bIndicatorEligible && Super::CanShowMeleeTargetIndicator_Implementation(InstigatorActor);
	}

	virtual void SetMeleeTargeted_Implementation(AActor* InstigatorActor, bool bTargeted) override
	{
		LastIndicatorInstigator = InstigatorActor;
		if (bTargeted)
		{
			++TargetedCount;
		}
		else
		{
			++UntargetedCount;
		}
	}

	virtual void HandleMeleeHitConfirmed_Implementation(const FMeleeHitContext& Context) override
	{
		LastConfirmedContext = Context;
		++ConfirmedCount;
	}

	virtual void HandleMeleeHitFeedback_Implementation(const FMeleeHitContext& Context) override
	{
		LastFeedbackContext = Context;
		++FeedbackCount;
	}

	void SetIndicatorEligible(bool bEligible) { bIndicatorEligible = bEligible; }
	int32 GetTargetedCount() const { return TargetedCount; }
	int32 GetUntargetedCount() const { return UntargetedCount; }
	int32 GetConfirmedCount() const { return ConfirmedCount; }
	int32 GetFeedbackCount() const { return FeedbackCount; }
	AActor* GetLastIndicatorInstigator() const { return LastIndicatorInstigator.Get(); }
	const FMeleeHitContext& GetLastConfirmedContext() const { return LastConfirmedContext; }

private:
	bool bIndicatorEligible = true;
	int32 TargetedCount = 0;
	int32 UntargetedCount = 0;
	int32 ConfirmedCount = 0;
	int32 FeedbackCount = 0;
	TWeakObjectPtr<AActor> LastIndicatorInstigator;
	FMeleeHitContext LastConfirmedContext;
	FMeleeHitContext LastFeedbackContext;
};
