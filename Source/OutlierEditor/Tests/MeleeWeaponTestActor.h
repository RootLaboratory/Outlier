#pragma once

#include "CoreMinimal.h"
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

	AActor* GetLastAppliedTarget() const { return LastAppliedTarget.Get(); }
	int32 GetAppliedTargetCount() const { return AppliedTargetCount; }

	virtual void ApplyHitToTarget(AActor* Target) override
	{
		LastAppliedTarget = Target;
		++AppliedTargetCount;
	}

private:
	TWeakObjectPtr<AActor> LastAppliedTarget;
	int32 AppliedTargetCount = 0;
};
